/*
 * PROC1_$CLR_LOCK - Release a resource lock
 *
 * Releases a lock by clearing a bit in the process's resource_locks_held
 * bitmask. The lock is identified by an ID (0-31).
 *
 * After releasing the lock:
 * - Decrements the inhibit count
 * - If no locks are held, handles deferred operations
 * - May cause a context switch if higher priority process is ready
 *
 * Crashes if the lock is not held by this process.
 *
 * Parameters:
 *   lock_id - Lock ID (0-31)
 *
 * Original address: 0x00e20b92
 *
 * TODO: This function manipulates SR for interrupt disable.
 *       The assembly wrapper handles that - see sau2/clr_lock.s
 */

#include "proc1.h"

/* Error status for releasing unheld lock */
static const status_$t Illegal_lock_err = 0x00000000;  /* TODO: Find actual value */

/* External function declarations */
extern void FUN_00e20824(void);  /* Unknown function called after removing from ready list */

/*
 * Internal implementation called with interrupts disabled.
 */
void proc1_$clr_lock_int(uint16_t lock_id)
{
    proc1_t *pcb = PROC1_$CURRENT_PCB;
    uint32_t mask = 1 << (lock_id & 0x1F);

    /* Verify lock is held */
    if ((pcb->resource_locks_held & mask) == 0) {
        CRASH_SYSTEM(&Illegal_lock_err);
    }

    /* Clear the lock bit */
    pcb->resource_locks_held &= ~mask;

    /* Decrement inhibit count */
    pcb->inh_count--;

    /* If inh_count reached zero, clear some flag (bit 0 of low byte of resource_locks_held?) */
    if (pcb->inh_count == 0) {
        /* Clear lowest byte bit 0 of resource_locks_held
         * This is unusual - might be a separate flag at offset 0x43 */
        uint8_t *flag_byte = ((uint8_t *)&pcb->resource_locks_held) + 3;  /* Low byte on m68k */
        *flag_byte &= 0xFE;
    }

    /* Reorder in ready list since our lock state changed */
    proc1_$reorder_if_needed(pcb);

    /* If all locks released, handle deferred operations */
    if (pcb->resource_locks_held == 0) {
        uint8_t flags = pcb->pri_max;

        /* Clear bit 4 (0x10) - some deferred flag */
        pcb->pri_max = flags & 0xEF;

        /* If bit 4 was set, do deferred ready list manipulation */
        if ((flags & 0x10) != 0) {
            proc1_$remove_from_ready_list(pcb);
            FUN_00e20824();
            /* pcb may have changed - would need to re-fetch in assembly */
        }

        /* If bit 2 (0x04) is set, try to suspend (deferred suspension) */
        if ((pcb->pri_max & 0x04) != 0) {
            PROC1_$TRY_TO_SUSPEND(pcb);
            pcb = PROC1_$CURRENT_PCB;
        }
    }

    /* Dispatch to potentially higher priority process */
    PROC1_$DISPATCH_INT2(pcb);
}
