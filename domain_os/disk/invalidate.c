/*
 * DISK_$INVALIDATE - Invalidate buffer cache entries for a volume
 *
 * This is a wrapper that acquires the disk lock, calls DBUF_$INVALIDATE,
 * then releases the lock.
 *
 * @param vol_idx  Volume index to invalidate
 */

#include "disk.h"

void DISK_$INVALIDATE(uint16_t vol_idx)
{
    ML_$LOCK(DISK_LOCK_ID);
    DBUF_$INVALIDATE(0, vol_idx);
    ML_$UNLOCK(DISK_LOCK_ID);
}
