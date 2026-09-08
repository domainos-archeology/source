/*
 * DISK_$GET_BLOCK - Get a disk block from the buffer cache
 *
 * This is a wrapper that acquires the disk lock, calls DBUF_$GET_BLOCK,
 * then releases the lock.
 *
 * @param vol_idx       Volume index
 * @param daddr         Disk address (block number)
 * @param expected_uid  Expected UID for validation
 * @param block_hint    DBUF_$GET_BLOCK's block hint, longword at (0x12,A6)
 * @param block_type    DBUF_$GET_BLOCK's block_type WORD, (0x16,A6)
 * @param flags         DBUF_$GET_BLOCK's flags WORD, (0x18,A6)
 * @param status        Output: Status code
 * @return Pointer to buffer, or NULL on error
 *
 * 0x00E3BB94-0x00E3BBB2 forwards all six arguments verbatim, the two words
 * at (0x16,A6) and (0x18,A6) among them.
 */

#include "disk/disk_internal.h"

void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *expected_uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    void *result;

    ML_$LOCK(DISK_LOCK_ID);
    result = DBUF_$GET_BLOCK(vol_idx, daddr, expected_uid, block_hint,
                             block_type, flags, status);
    ML_$UNLOCK(DISK_LOCK_ID);

    return result;
}
