/*
 * DISK_$DISMOUNT - Dismount a volume and shut its drive down when idle
 *
 * 0x00E6CFEA - 0x00E6D12E (326 bytes).  Verified against the disassembly
 * on 2026-09-19; the earlier emission was faithful, this one cites the
 * address ranges and the two Pascal result slots it ignores.  The prologue
 * loads A5 with 0xE826C4 (the `D E826C4 DISK_ size = 64` block) but the
 * body never references A5.
 *
 * Argument: (0x8,A6) vol_idx, word by value (D2).
 *
 * Under MOUNT_LOCK the volume's cache is invalidated and, if it is an LV
 * (lv_start != 0), its mount state cleared.  The ten descriptors are then
 * scanned for others on the same drive (same dev_info AND dev_unit): any
 * mounted/assigned LV among them keeps the drive up.  With none left, the
 * drive is shut down through the PV descriptor that was found (a missing
 * PV is a driver logic error) and every partition of that PV is
 * invalidated and unmounted.
 */

#include "disk/disk_internal.h"
#include "misc/misc.h"

/*
 * 0x00E6D130: 00 08 00 22, passed by `pea (0x82,PC)` at 0x00E6D0AC to
 * CRASH_SYSTEM (0x00E6D0B0).  stcode.db.10.2: 0x00080022 "driver logic
 * error".
 */
static const status_$t disk_$driver_logic_error_00e6d130 = 0x00080022;

/* 0x00E6D058: `moveq #0x9,D1` / dbf - descriptors 1..10 */
#define DISK_DISMOUNT_SCAN_COUNT  10

void DISK_$DISMOUNT(uint16_t vol_idx)
{
    disk_$volume_t *vol;        /* A0 */
    disk_$volume_t *entry;      /* A2 */
    disk_$volume_t *pv;         /* A4 */
    int16_t state;              /* D3 */
    int16_t lv_count;           /* D0 */
    int16_t pv_idx;             /* D2 */
    int16_t idx;                /* D3 in the scan */
    int16_t i;
    int16_t parts;
    int16_t part;
    int16_t sub_vol;

    /* 0x00E6CFF8 - 0x00E6D008: `btst.l D0,D1` against 0x7fe, modulo 32;
     * an invalid index returns without taking the lock */
    if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        return;
    }

    /* 0x00E6D00C - 0x00E6D018 */
    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /* 0x00E6D01A - 0x00E6D024: a Pascal function - the `subq.l #2,SP`
     * result slot is discarded */
    DISK_$INVALIDATE(vol_idx);

    /* 0x00E6D026 - 0x00E6D048: 0xE7A290 + vol_idx * 0x48, word arithmetic */
    vol = DISK_VOL(vol_idx);
    state = (int16_t)vol->mount_state;
    if (state == DISK_MOUNT_MOUNTED || state == DISK_MOUNT_ASSIGNED) {
        /* 0x00E6D04C - 0x00E6D052: an LV is simply unmounted */
        if (vol->lv_start != 0) {
            vol->mount_state = 0;
        }

        /* 0x00E6D056 - 0x00E6D0A0: scan descriptors 1..10 */
        pv_idx = 0;
        lv_count = 0;
        idx = 1;
        for (i = 0; i < DISK_DISMOUNT_SCAN_COUNT; i++) {
            int16_t s;
            entry = DISK_VOL(idx);                                  /* A1 += 0x48 */
            s = (int16_t)entry->mount_state;
            if ((s == DISK_MOUNT_ASSIGNED || s == DISK_MOUNT_MOUNTED) &&
                entry->dev_info == vol->dev_info &&                 /* 0x00E6D07E */
                entry->dev_unit == vol->dev_unit) {                 /* 0x00E6D088 */
                if (entry->lv_start != 0) {                         /* 0x00E6D08E */
                    lv_count++;
                } else {
                    pv_idx = idx;                                   /* 0x00E6D098 */
                }
            }
            idx++;
        }

        /* 0x00E6D0A4 - 0x00E6D0A6: another LV still uses the drive */
        if (lv_count == 0) {
            /* 0x00E6D0A8 - 0x00E6D0B6 */
            if (pv_idx == 0) {
                CRASH_SYSTEM(&disk_$driver_logic_error_00e6d130);
            }

            /* 0x00E6D0B8 - 0x00E6D0DE: long arithmetic this time
             * (ext.l / lsl.l); `subq.l #2,SP` result slot discarded */
            pv = DISK_VOL(pv_idx);
            DISK_$SHUTDOWN((disk_device_entry_t *)pv->dev_info, pv->dev_unit);

            /* 0x00E6D0E0 - 0x00E6D116: num_parts (-0x1c) entries of
             * part_volx from index 1 ((-0x12,A4) + 2), each invalidated
             * (result slot discarded) and its mount state cleared */
            parts = (int16_t)pv->num_parts;
            if ((int16_t)(parts - 1) >= 0) {
                part = 1;
                for (i = 0; i < parts; i++) {
                    sub_vol = (int16_t)pv->part_volx[part];
                    DISK_$INVALIDATE((uint16_t)sub_vol);
                    DISK_VOL(sub_vol)->mount_state = 0;             /* 0x00E6D110 */
                    part++;
                }
            }
        }
    }

    /* 0x00E6D11A - 0x00E6D120 */
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
}
