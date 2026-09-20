/*
 * DISK_$GET_BLOCK - DBUF_$GET_BLOCK under the disk resource lock
 *
 * 0x00E3BB80 - 0x00E3BBD2 (84 bytes).  Verified against the disassembly on
 * 2026-09-19; the earlier emission was faithful.
 *
 * ML_$LOCK(15) (0x00E3BB86 - 0x00E3BB92, result slot discarded), then the
 * seven arguments are pushed again in the same order (0x00E3BB94 -
 * 0x00E3BBB2: status (0x1a), flags word (0x18), block_type word (0x16),
 * block_hint (0x12), uid (0xe), daddr (0xa), vol_idx word (0x8)), the
 * pointer result comes back in A0 and is kept in A2 across
 * ML_$UNLOCK(15) (0x00E3BBBE - 0x00E3BBC4), then returned in A0.
 */

#include "disk/disk_internal.h"
#include "dbuf/dbuf.h"
#include "ml/ml.h"

void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *expected_uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    void *result;

    ML_$LOCK(DISK_LOCK_ID);
    result = DBUF_$GET_BLOCK((uint16_t)vol_idx, daddr, (uid_t *)expected_uid,
                             block_hint, block_type, flags, status);
    ML_$UNLOCK(DISK_LOCK_ID);

    return result;
}
