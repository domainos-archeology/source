/*
 * DISK_$LVUID_TO_VOLX - Find the mounted logical volume with a given UID
 *
 * 0x00E6D134 - 0x00E6D1C8 (150 bytes).  Verified against the disassembly
 * on 2026-09-19; the earlier emission stepped a host pointer through the
 * table (the image steps 0x48) and is otherwise faithful.  The prologue
 * loads A5 = 0xE826C4 (the second `DISK_` data block) but never uses it.
 *
 * Arguments:
 *   (0x8,A6)  uid_ptr  -> uid_t, copied to (-0x8,A6) first
 *   (0xc,A6)  vol_idx  -> word out
 *   (0x10,A6) status   -> status_$t out
 *
 * Descriptors 1..6 (`moveq #0x5` / dbf) whose state is mounted (3) and
 * whose lv_start is non-zero are compared on lv_uid (the descriptor's
 * first two longwords, `cmpm.l` twice).  The result word is D2: the
 * matching index, or - when no candidate matched - 1 if at least one
 * candidate was compared (`moveq #0x1,D2` at 0x00E6D18A runs before each
 * compare) and otherwise the caller's D2.  Modelled as 1 here.
 */

#include "disk/disk_internal.h"
#include "ml/ml.h"

void DISK_$LVUID_TO_VOLX(void *uid_ptr, int16_t *vol_idx, status_$t *status)
{
    uid_t uid;                      /* (-0x8,A6) */
    status_$t local_status;         /* (-0xc,A6) */
    int16_t d2 = 1;                 /* D2 */
    int16_t idx;                    /* D1 */
    int16_t i;

    /* 0x00E6D142 - 0x00E6D14A */
    uid.high = ((const uint32_t *)uid_ptr)[0];
    uid.low = ((const uint32_t *)uid_ptr)[1];

    /* 0x00E6D14E - 0x00E6D15C */
    ML_$EXCLUSION_START(&MOUNT_LOCK);
    local_status = status_$logical_volume_not_found;

    /* 0x00E6D164 - 0x00E6D1A2 */
    idx = 1;
    for (i = 0; i < VOL_TABLE_SCAN_COUNT; i++) {
        disk_$volume_t *vol = DISK_VOL(idx);
        if (vol->mount_state == DISK_MOUNT_MOUNTED && vol->lv_start != 0) {
            d2 = 1;                                             /* 0x00E6D18A */
            if (vol->lv_uid.high == uid.high && vol->lv_uid.low == uid.low) {
                local_status = status_$ok;                      /* 0x00E6D194 */
                d2 = idx;
                break;
            }
        }
        idx++;
    }

    /* 0x00E6D1A6 - 0x00E6D1BC */
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
    *vol_idx = d2;
    *status = local_status;
}
