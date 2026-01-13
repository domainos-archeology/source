/*
 * PROC1_$TST_LOCK - Test if a lock is held
 *
 * Parameters:
 *   lock_id - Lock ID to test (0-31)
 *
 * Returns:
 *   Non-zero if lock is held by current process
 *
 * Original address: 0x00e148ca
 */

#include "proc1.h"

int16_t PROC1_$TST_LOCK(uint16_t lock_id)
{
    uint32_t mask = 1 << (lock_id & 0x1F);
    return (PROC1_$CURRENT_PCB->resource_locks_held & mask) != 0;
}
