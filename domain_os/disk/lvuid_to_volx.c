/*
 * DISK_$LVUID_TO_VOLX - Convert logical volume UID to volume index
 *
 * Searches the mounted volume table for a volume with the given
 * logical volume UID and returns its index.
 *
 * @param uid_ptr   Pointer to UID (8 bytes)
 * @param vol_idx   Output: Volume index (1-based)
 * @param status    Output: Status code
 */

#include "disk/disk_internal.h"

/* Status code */
#define status_$logical_volume_not_found  0x00080010

/* disk_$volume_t and DISK_VOL() come from disk/disk_internal.h */

void DISK_$LVUID_TO_VOLX(void *uid_ptr, int16_t *vol_idx, status_$t *status)
{
    uint32_t uid_hi, uid_lo;
    int16_t i;
    int16_t result_idx = 1;  /* Default if not found */
    disk_$volume_t *entry;
    status_$t local_status;

    /* Get UID to search for */
    uid_hi = *(uint32_t *)uid_ptr;
    uid_lo = *((uint32_t *)uid_ptr + 1);

    ML_$EXCLUSION_START(&MOUNT_LOCK);

    local_status = status_$logical_volume_not_found;

    /* Search volumes 1-6 */
    entry = DISK_VOL(1);  /* Start at volume 1 */
    for (i = 5; i >= 0; i--) {
        /* Check if volume is mounted (state == 3) and has LV data */
        if ((int16_t)entry->mount_state == DISK_MOUNT_MOUNTED &&
            entry->lv_start != 0) {

            /* Compare UIDs */
            if (entry->lv_uid.high == uid_hi && entry->lv_uid.low == uid_lo) {
                local_status = status_$ok;
                result_idx = 6 - i;  /* Convert loop counter to 1-based index */
                break;
            }
        }
        entry++;
    }

    ML_$EXCLUSION_STOP(&MOUNT_LOCK);

    *vol_idx = result_idx;
    *status = local_status;
}
