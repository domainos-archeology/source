/*
 * DISK_$LV_UID - Read the UID of one logical volume off a mounted PV
 *
 * 0x00E6CC62 - 0x00E6CDB0 (336 bytes).  Verified against the disassembly
 * on 2026-09-19; the earlier emission was faithful but addressed the
 * descriptor by private offsets.  The prologue loads A5 = 0xE826C4 (the
 * second `DISK_` data block) but never uses it.
 *
 * Arguments:
 *   (0x8,A6)  vol_idx     word by value (D2) - NOT range checked
 *   (0xa,A6)  lv_num      word by value (D3), 1..10
 *   (0xc,A6)  uid_ret     -> uid_t, ALWAYS written from the frame cells
 *                          (-0x8,A6)/(-0x4,A6), which are only assigned on
 *                          the success path (0x00E6CD68); on every error
 *                          path the caller receives whatever the frame held
 *   (0x10,A6) status_ret  -> status_$t
 *
 * Both DISK_$GET_BLOCK calls tolerate "storage module stopped"
 * (0x0008001B); the label blocks are released with DISK_$SET_BUFF(…, 8)
 * and (…, 0xC), each with a four-byte frame cell (-0xc,A6) as its third
 * argument.
 */

#include "disk/disk_internal.h"
#include "ml/ml.h"
#include "uid/uid.h"

/* PV label: the LV start-address table at +0x38, 1-based by lv_num
 * (0x00E6CD08 `move.l (0x38,A3,D0*0x1)`, D0 = lv_num * 4). */
#define DISK_PV_LABEL_LV_TABLE   0x38
/* LV label: the volume UID at +0x24 (0x00E6CD64). */
#define DISK_LV_LABEL_UID        0x24

void DISK_$LV_UID(int16_t vol_idx, int16_t lv_num, uid_t *uid_ret,
                  status_$t *status_ret)
{
    status_$t status;               /* (-0x10,A6) */
    uid_t lv_uid;                   /* (-0x8,A6): not initialised, see above */
    uint32_t set_buff_cell;         /* (-0xc,A6) */
    disk_$volume_t *vol;            /* A2 */
    uint8_t *block;                 /* A3 / A2 */
    uint32_t lv_daddr;              /* D3 */

    /* 0x00E6CC78 - 0x00E6CC84 */
    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /* 0x00E6CC86 - 0x00E6CCAA: 0xE7A290 + vol_idx * 0x48, word arithmetic */
    vol = DISK_VOL(vol_idx);
    if (vol->mount_state != DISK_MOUNT_BUSY) {
        status = status_$volume_not_properly_mounted;
        goto done;
    }

    /* 0x00E6CCAE - 0x00E6CCBC */
    if (vol->lv_start != 0) {
        status = status_$operation_requires_a_physical_volume;
        goto done;
    }

    /* 0x00E6CCC0 - 0x00E6CCC8: `bhi` - unsigned */
    if (lv_num == 0 || (uint16_t)lv_num > MAX_LV_INDEX) {
        status = status_$invalid_logical_volume_index;      /* 0x00E6CD2A */
        goto done;
    }

    /* 0x00E6CCCA - 0x00E6CCF8: `pea (0x20).w` is the longword 0x00000020,
     * read back as block_type 0 and flags 0x20 */
    block = (uint8_t *)DISK_$GET_BLOCK(vol_idx, 0, &PV_LABEL_$UID, 0, 0,
                                       DBUF_GET_OK_IF_STOPPED, &status);
    if (status != status_$ok && status != status_$storage_module_stopped) {
        goto done;
    }

    /* 0x00E6CCFC - 0x00E6CD1C: fetch the entry, then release the block */
    lv_daddr = *(uint32_t *)(block + DISK_PV_LABEL_LV_TABLE + (uint16_t)lv_num * 4);
    DISK_$SET_BUFF(block, 0x08, &set_buff_cell);

    /* 0x00E6CD20 - 0x00E6CD32: `bls` on (-0x3c,A2) = addr_start */
    if (lv_daddr == 0 || lv_daddr > vol->addr_start) {
        status = status_$invalid_logical_volume_index;
        goto done;
    }

    /* 0x00E6CD34 - 0x00E6CD62 */
    block = (uint8_t *)DISK_$GET_BLOCK(vol_idx, (int32_t)lv_daddr, &LV_LABEL_$UID,
                                       0, 0, DBUF_GET_OK_IF_STOPPED, &status);
    if (status != status_$ok && status != status_$storage_module_stopped) {
        goto done;
    }

    /* 0x00E6CD64 - 0x00E6CD82 */
    lv_uid.high = *(uint32_t *)(block + DISK_LV_LABEL_UID);
    lv_uid.low = *(uint32_t *)(block + DISK_LV_LABEL_UID + 4);
    DISK_$SET_BUFF(block, 0x0c, &set_buff_cell);

done:
    /* 0x00E6CD86 - 0x00E6CDA4 */
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
    uid_ret->high = lv_uid.high;
    uid_ret->low = lv_uid.low;
    *status_ret = status;
}
