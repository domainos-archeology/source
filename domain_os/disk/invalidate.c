/*
 * DISK_$INVALIDATE - DBUF_$INVALIDATE for a whole volume under the disk lock
 *
 * 0x00E3BC0E - 0x00E3BC3E (50 bytes).  Verified against the disassembly on
 * 2026-09-19; the earlier emission was faithful.
 *
 * Argument: (0x8,A6) vol_idx, word by value.
 *
 * ML_$LOCK(15) (0x00E3BC12 - 0x00E3BC1E), DBUF_$INVALIDATE(0, vol_idx)
 * (`clr.l -(SP)` is the longword block argument, 0x00E3BC20 - 0x00E3BC2E),
 * ML_$UNLOCK(15).  All three calls open a Pascal result slot that is
 * discarded; the callers of DISK_$INVALIDATE (DISK_$DISMOUNT and the
 * mount path) discard this routine's D0 the same way.
 */

#include "disk/disk_internal.h"
#include "dbuf/dbuf.h"
#include "ml/ml.h"

void DISK_$INVALIDATE(uint16_t vol_idx)
{
    ML_$LOCK(DISK_LOCK_ID);
    DBUF_$INVALIDATE(0, vol_idx);
    ML_$UNLOCK(DISK_LOCK_ID);
}
