/*
 * RIP_$LOCK / RIP_$UNLOCK - RIP subsystem locking
 *
 * These functions provide mutual exclusion for the RIP routing table.
 * They use a combination of priority locking (PROC1_$SET_LOCK) and
 * exclusion locks (ML_$EXCLUSION_START/STOP).
 *
 * Original addresses:
 *   RIP_$LOCK:   0x00E154A4
 *   RIP_$UNLOCK: 0x00E154C4
 */

#include "rip/rip_internal.h"

/*
 * RIP_$LOCK - Acquire RIP subsystem lock
 *
 * Raises process priority to prevent preemption during routing table
 * operations, then acquires the exclusion lock.
 */
void RIP_$LOCK(void)
{
    /* Raise process priority to lock level 0x0E */
    PROC1_$SET_LOCK(RIP_LOCK_PRIORITY);

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&RIP_$DATA.exclusion);
}

/*
 * RIP_$UNLOCK - Release RIP subsystem lock
 *
 * Releases the exclusion lock and restores process priority.
 */
void RIP_$UNLOCK(void)
{
    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&RIP_$DATA.exclusion);

    /* Restore process priority */
    PROC1_$CLR_LOCK(RIP_LOCK_PRIORITY);
}
