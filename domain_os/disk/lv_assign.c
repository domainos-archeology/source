/*
 * DISK_$LV_ASSIGN - Assign one logical volume of an assigned/mounted PV
 *
 * 0x00E6CDB2 - 0x00E6CFE8 (568 bytes).  Re-emitted from the disassembly
 * on 2026-09-19.  Wrong before: DISK_$SET_BUFF's third argument was NULL
 * (the image passes the frame cell `pea (-0x8,A6)`, 0x00E6CEE4), the
 * error-path result and blocks_avail values were invented zeros, and the
 * scan stepped a host pointer instead of the 0x48 stride.  The prologue
 * loads A5 = 0xE826C4 (the second `DISK_` data block) but never uses it.
 *
 * Arguments (all by reference):
 *   (0x8,A6)  vol_idx_ptr       -> word PV index (D4)
 *   (0xc,A6)  lv_idx_ptr        -> word LV number 1..10 (D3)
 *   (0x10,A6) blocks_avail_ptr  -> longword out (D5)
 *   (0x14,A6) status            -> status_$t
 * Result: D2 as a word.  It is the new volume index on success; on the
 * two pre-lock errors D2 is not assigned at all (the caller's D2 comes
 * back), on the errors before the label is read its high byte is bits
 * 8..15 of vol_idx*64 and its low byte the boolean of 0x00E6CE52 (0), and
 * on the later errors it is the low word of the LV start address.  The
 * register is modelled below as `d2`.
 *
 * Frame: (-0x42,A6) vol_idx*0x48, (-0x20,A6) free slot, (-0x14,A6) LV
 * size, (-0x10,A6) status, (-0x8,A6) DISK_$SET_BUFF's cell.
 */

#include "disk/disk_internal.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "uid/uid.h"

/* PV label: LV start addresses at +0x38 (1-based by LV number, so entry
 * n+1 is at +0x3c + n*4) and LV end addresses at +0x60. */
#define DISK_PV_LABEL_LV_START   0x38
#define DISK_PV_LABEL_LV_END     0x60
/* 0x00E6CE02: `move.w #0x5,-(SP)` to PROC2_$SET_CLEANUP */
#define DISK_LV_ASSIGN_CLEANUP   5

uint16_t DISK_$LV_ASSIGN(uint16_t *vol_idx_ptr, uint16_t *lv_idx_ptr,
                         int32_t *blocks_avail_ptr, status_$t *status)
{
    uint16_t vol_idx;               /* D4 */
    uint16_t lv_idx;                /* D3 */
    disk_$volume_t *vol;            /* A2 */
    disk_$volume_t *scan;           /* A0 in the scan */
    disk_$volume_t *dest;           /* A0 after 0x00E6CF82 */
    uint8_t *block;                 /* A3 */
    status_$t local_status;         /* (-0x10,A6) */
    uint16_t saved_state;           /* D6 */
    uint32_t lv_start;              /* D2 after 0x00E6CEA8 */
    int32_t lv_size;                /* (-0x14,A6) */
    uint32_t set_buff_cell;         /* (-0x8,A6) */
    uint16_t free_slot;             /* (-0x20,A6) */
    uint32_t d2 = 0;                /* D2: see the header comment */
    uint32_t d5 = 0;                /* D5: blocks_avail, see below */
    int8_t ok;                      /* D2b at 0x00E6CE3C - 0x00E6CE54 */
    int16_t i;
    int16_t slot;

    /* 0x00E6CDC0 - 0x00E6CDE4: `btst.l D0,D1` against 0x7fe, modulo 32;
     * straight to the exit, blocks_avail not written */
    vol_idx = *vol_idx_ptr;
    lv_idx = *lv_idx_ptr;
    if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;
        return (uint16_t)d2;
    }

    /* 0x00E6CDE8 - 0x00E6CDFC: `bhi` / `tst.w` */
    if (lv_idx > MAX_LV_INDEX || lv_idx == 0) {
        *status = status_$invalid_logical_volume_index;
        return (uint16_t)d2;
    }

    /* 0x00E6CE00 - 0x00E6CE1A: result slot of SET_CLEANUP discarded */
    PROC2_$SET_CLEANUP(DISK_LV_ASSIGN_CLEANUP);
    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /* 0x00E6CE1C - 0x00E6CE30: D2w = vol_idx * 64 on the way to * 0x48 */
    d2 = (uint16_t)(vol_idx << 6);
    vol = DISK_VOL(vol_idx);

    /* 0x00E6CE34 - 0x00E6CE5E: assigned to this process, or busy.  D5w is
     * loaded with mount_proc and its low byte reused for the booleans. */
    saved_state = vol->mount_state;
    d5 = (uint16_t)vol->mount_proc;
    ok = (saved_state == DISK_MOUNT_ASSIGNED) ? -1 : 0;
    d5 = (d5 & 0xffffff00u) | ((d5 & 0xffffu) == PROC1_$CURRENT ? 0xffu : 0u);
    ok &= (int8_t)(d5 & 0xff);
    d5 = (d5 & 0xffffff00u) | (saved_state == DISK_MOUNT_BUSY ? 0xffu : 0u);
    ok |= (int8_t)(d5 & 0xff);
    d2 = (d2 & 0xffffff00u) | (uint8_t)ok;
    if (ok >= 0) {
        local_status = status_$volume_not_properly_mounted;
        goto done;
    }

    /* 0x00E6CE62 - 0x00E6CE70 */
    if (vol->lv_start != 0) {
        local_status = status_$operation_requires_a_physical_volume;
        goto done;
    }

    /* 0x00E6CE74: busy while the label is read; restored at `done` */
    vol->mount_state = DISK_MOUNT_BUSY;

    /* 0x00E6CE7A - 0x00E6CE9E: two `clr.l` = block_hint 0 and the two
     * zero argument words; a stopped storage module is NOT tolerated here */
    block = (uint8_t *)DISK_$GET_BLOCK((int16_t)vol_idx, 0, &PV_LABEL_$UID,
                                       0, 0, 0, &local_status);
    if (local_status != status_$ok) {
        goto done;
    }

    /* 0x00E6CEA2 - 0x00E6CEBA: start address, and the end-address entry
     * turned into a block count when present */
    lv_start = *(uint32_t *)(block + DISK_PV_LABEL_LV_START + lv_idx * 4);
    d2 = lv_start;
    d5 = *(uint32_t *)(block + DISK_PV_LABEL_LV_END + lv_idx * 4);
    if (d5 != 0) {
        d5 -= lv_start;
    }

    /* 0x00E6CEBC - 0x00E6CEDE: size from the next LV's start (`bcc` on
     * lv_idx >= 10 skips the lookup), else from the PV's addr_start */
    if (lv_idx < 10 &&
        *(uint32_t *)(block + DISK_PV_LABEL_LV_START + 4 + lv_idx * 4) != 0) {
        lv_size = (int32_t)(*(uint32_t *)(block + DISK_PV_LABEL_LV_START + 4 + lv_idx * 4)
                            - lv_start);
    } else {
        lv_size = (int32_t)(vol->addr_start - lv_start);
    }

    /* 0x00E6CEE2 - 0x00E6CEF4 */
    DISK_$SET_BUFF(block, 0x0c, &set_buff_cell);

    /* 0x00E6CEF8 - 0x00E6CF0A: `bls` on addr_start */
    if (lv_start == 0 || lv_start > vol->addr_start) {
        local_status = status_$invalid_logical_volume_index;
        goto done;
    }

    /* 0x00E6CF0E - 0x00E6CF5C: scan descriptors 6 down to 1.  A free
     * slot is remembered (the LAST one seen, i.e. the lowest index); a
     * live descriptor on the same drive with this LV start is "in use". */
    free_slot = 0;
    slot = VOL_TABLE_SCAN_COUNT;
    for (i = 0; i < VOL_TABLE_SCAN_COUNT; i++) {
        scan = DISK_VOL(slot);
        if (scan->mount_state == DISK_MOUNT_FREE) {
            free_slot = (uint16_t)slot;                     /* 0x00E6CF2C */
        } else if (scan->dev_unit == vol->dev_unit &&       /* 0x00E6CF32 */
                   scan->lv_start == lv_start &&            /* 0x00E6CF3C */
                   scan->dev_info == vol->dev_info) {       /* 0x00E6CF42 */
            local_status = status_$volume_in_use;
            goto done;
        }
        slot--;
    }

    /* 0x00E6CF60 - 0x00E6CF6E */
    if (free_slot == 0) {
        local_status = status_$volume_table_full;
        goto done;
    }

    /* 0x00E6CF70 - 0x00E6CF92: copy the whole 0x48-byte descriptor
     * (`moveq #0x11` / dbf = 18 longwords) */
    dest = DISK_VOL(free_slot);
    for (i = 0; i < 18; i++) {
        ((uint32_t *)dest)[i] = ((uint32_t *)vol)[i];
    }

    /* 0x00E6CF96 - 0x00E6CFB2 */
    dest->lv_start = lv_start;
    dest->addr_start = (uint32_t)lv_size;
    dest->mount_proc = (int16_t)PROC1_$CURRENT;
    dest->as_options = 0;
    dest->mount_state = DISK_MOUNT_ASSIGNED;
    d2 = free_slot;
    /* local_status is still the status_$ok DISK_$GET_BLOCK returned */

done:
    /* 0x00E6CFB6 - 0x00E6CFDA: the PV's state back, unlock, both outputs */
    vol->mount_state = saved_state;
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
    *blocks_avail_ptr = (int32_t)d5;
    *status = local_status;
    return (uint16_t)d2;
}
