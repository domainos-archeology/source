/*
 * FILE_$NEIGHBORS - Check if two files are on the same volume (neighbors)
 *
 * Wrapper function that checks if two files are neighbors (on the same volume).
 * Calls FILE_$CHECK_SAME_VOLUME with copy_location=0.
 *
 * Original address: 0x00E5E5AE
 */

#include "file/file_internal.h"

/*
 * FILE_$NEIGHBORS
 *
 * Checks if two files are located on the same volume.
 *
 * Parameters:
 *   file_uid1  - First file UID
 *   file_uid2  - Second file UID
 *   status_ret - Output status code
 *
 * Flow:
 * 1. Copy both UIDs to local stack storage
 * 2. Call FILE_$CHECK_SAME_VOLUME with copy_location=0
 * 3. The result indicates whether the files are neighbors
 */
void FILE_$NEIGHBORS(uid_t *file_uid1, uid_t *file_uid2, status_$t *status_ret)
{
    uid_t local_uid1;
    uid_t local_uid2;
    uint32_t location_buf[8];  /* 32 bytes for location info */

    /* Copy UIDs to local storage */
    local_uid1.high = file_uid1->high;
    local_uid1.low = file_uid1->low;
    local_uid2.high = file_uid2->high;
    local_uid2.low = file_uid2->low;

    /* Check if files are on the same volume (copy_location=0) */
    FILE_$CHECK_SAME_VOLUME(&local_uid1, &local_uid2, 0, location_buf, status_ret);
}
