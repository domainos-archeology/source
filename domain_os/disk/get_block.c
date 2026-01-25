/*
 * DISK_$GET_BLOCK - Get a disk block from the buffer cache
 *
 * This is a wrapper that acquires the disk lock, calls DBUF_$GET_BLOCK,
 * then releases the lock.
 *
 * @param vol_idx       Volume index
 * @param daddr         Disk address (block number)
 * @param expected_uid  Expected UID for validation
 * @param param_4       Additional parameter
 * @param param_5       Additional parameter
 * @param status        Output: Status code
 * @return Pointer to buffer, or NULL on error
 */

#include "disk_internal.h"

void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *expected_uid,
                      uint16_t param_4, uint16_t param_5, status_$t *status)
{
    void *result;

    ML_$LOCK(DISK_LOCK_ID);
    result = DBUF_$GET_BLOCK(vol_idx, daddr, expected_uid, param_4, param_5, status);
    ML_$UNLOCK(DISK_LOCK_ID);

    return result;
}
