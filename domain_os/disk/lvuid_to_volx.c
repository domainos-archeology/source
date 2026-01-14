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

#include "disk.h"

/* Mount lock */
extern void *MOUNT_LOCK;

/* Status code */
#define status_$logical_volume_not_found  0x00080010

/* Volume table base for mounted state check */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a290)

/* Mount state offset */
#define DISK_MOUNT_STATE_OFFSET  (-0x34)  /* -0x34 from +0x48 = 0x14 offset */

/* UID offset in volume entry */
#define DISK_UID_OFFSET          (-0x48)

/* LV data offset */
#define DISK_LV_DATA_OFFSET      (-0x40)

/* ML_$EXCLUSION_START, ML_$EXCLUSION_STOP declared in ml/ml.h via disk.h */

void DISK_$LVUID_TO_VOLX(void *uid_ptr, int16_t *vol_idx, status_$t *status)
{
    uint32_t uid_hi, uid_lo;
    int16_t i;
    int16_t result_idx = 1;  /* Default if not found */
    uint8_t *entry;
    status_$t local_status;

    /* Get UID to search for */
    uid_hi = *(uint32_t *)uid_ptr;
    uid_lo = *((uint32_t *)uid_ptr + 1);

    ML_$EXCLUSION_START(&MOUNT_LOCK);

    local_status = status_$logical_volume_not_found;

    /* Search volumes 1-6 */
    entry = DISK_VOLUME_BASE + DISK_VOLUME_SIZE;  /* Start at volume 1 */
    for (i = 5; i >= 0; i--) {
        /* Check if volume is mounted (state == 3) and has LV data */
        if (*(int16_t *)(entry + DISK_MOUNT_STATE_OFFSET) == DISK_MOUNT_MOUNTED &&
            *(uint32_t *)(entry + DISK_LV_DATA_OFFSET) != 0) {

            /* Compare UIDs */
            if (*(uint32_t *)(entry + DISK_UID_OFFSET) == uid_hi &&
                *(uint32_t *)(entry + DISK_UID_OFFSET + 4) == uid_lo) {
                local_status = status_$ok;
                result_idx = 6 - i;  /* Convert loop counter to 1-based index */
                break;
            }
        }
        entry += DISK_VOLUME_SIZE;
    }

    ML_$EXCLUSION_STOP(&MOUNT_LOCK);

    *vol_idx = result_idx;
    *status = local_status;
}
