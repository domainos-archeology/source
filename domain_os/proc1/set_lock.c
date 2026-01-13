/*
 * PROC1_$SET_LOCK - Acquire a resource lock
 *
 * Acquires a lock by setting a bit in the process's resource_locks_held
 * bitmask. The lock is identified by an ID (0-31).
 *
 * If the new lock has higher priority (higher bit position) than any
 * currently held lock, the process may be reordered in the ready list.
 *
 * Crashes if the lock is already held by this process.
 *
 * Parameters:
 *   lock_id - Lock ID (0-31)
 *
 * Original address: 0x00e20ae4
 *
 * TODO: This function manipulates SR for interrupt disable.
 *       The assembly wrapper handles that - see sau2/set_lock.s
 */

#include "proc1.h"

/* Error status for lock ordering violation */
static const status_$t Lock_order_violation_err = 0x000E2DE4;

/*
 * Internal implementation called with interrupts disabled.
 * A1 = current PCB pointer on entry (m68k convention).
 */
void proc1_$set_lock_int(uint16_t lock_id)
{
    proc1_t *pcb = PROC1_$CURRENT_PCB;
    uint32_t mask = 1 << (lock_id & 0x1F);

    /* Increment inhibit count - prevents preemption */
    pcb->inh_count++;

    /*
     * Check lock ordering: locks must be acquired in order of
     * increasing bit position. If we already hold a higher-numbered
     * lock, this is a lock ordering violation.
     *
     * The comparison resource_locks_held < mask checks if all
     * currently held locks have lower bit positions than the
     * one we're trying to acquire.
     */
    if (pcb->resource_locks_held < mask) {
        /* Valid lock acquisition - set the bit */
        pcb->resource_locks_held |= mask;

        /* If this process is not currently running, reorder in ready list */
        if (pcb != PROC1_$READY_PCB) {
            proc1_$reorder_if_needed(pcb);
        }
        return;
    }

    /* Lock ordering violation - crash */
    CRASH_SYSTEM(&Lock_order_violation_err);
}
