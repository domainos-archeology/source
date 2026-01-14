/*
 * ML_$UNLOCK - Release a resource lock
 *
 * This function releases a resource lock and wakes any waiting processes.
 * May trigger rescheduling if a higher-priority process was waiting.
 *
 * Original address: 0x00E20B62
 *
 * The function shares code with ML_$EXCLUSION_STOP for the common
 * exit path (inhibit count decrement and rescheduling).
 */

#include "ml.h"
#include "proc1/proc1.h"

/* External status for crash */
extern status_$t Illegal_lock_err;

/* Internal: advance lock event count */
extern void ADVANCE_INT(ec_$eventcount_t *ec);

/* Internal: process scheduling helper */
extern void FUN_00e20824(void);

void ML_$UNLOCK(int16_t resource_id)
{
    proc1_t *pcb;
    uint32_t lock_mask;
    int16_t ec_offset;
    uint8_t pri_flags;

    uint16_t sr;
    DISABLE_INTERRUPTS(sr);

    /* Clear the lock byte */
    ML_$LOCK_BYTES[resource_id] &= ~0x01;

    /*
     * If there are waiters (event count != wait count), advance the
     * event count to wake them up.
     */
    ec_offset = resource_id << 4;
    if (((ec_$eventcount_t *)((char *)ML_$LOCK_EVENTS + ec_offset))->count !=
        ((int32_t *)((char *)ML_$LOCK_EVENTS + ec_offset + 0x0C))[0]) {
        ADVANCE_INT((ec_$eventcount_t *)((char *)ML_$LOCK_EVENTS + ec_offset));
    }

    pcb = PROC1_$CURRENT_PCB;

    /* Calculate lock mask for this resource */
    lock_mask = 1U << (resource_id & 0x1F);

    /* Verify we actually hold this lock */
    if ((pcb->resource_locks_held & lock_mask) == 0) {
        CRASH_SYSTEM(&Illegal_lock_err);
    }

    /* Clear this lock from held locks */
    pcb->resource_locks_held &= ~lock_mask;

    /* Decrement inhibit count */
    pcb->pad_5a--;

    if (pcb->pad_5a == 0) {
        /* Clear bit 0 of resource_locks_held byte at offset 0x43 */
        /* This is the low byte of the high word - indicates "has any locks" */
        *((uint8_t *)&pcb->resource_locks_held + 3) &= ~0x01;
    }

    /* Reorder process in ready list */
    proc1_$reorder_if_needed(pcb);

    /* Check if we need to handle deferred operations */
    if (pcb->resource_locks_held == 0) {
        pri_flags = *((uint8_t *)&pcb->pri_max);

        /* Clear priority boost flag (bit 4) */
        *((uint8_t *)&pcb->pri_max) = pri_flags & ~0x10;

        if (pri_flags & 0x10) {
            /* Was priority boosted - remove from ready list and reschedule */
            proc1_$remove_from_ready_list(pcb);
            FUN_00e20824();
        }

        /* Check for deferred suspend (bit 2 at offset 0x55) */
        if (pcb->pri_max & 0x400) {
            PROC1_$TRY_TO_SUSPEND(pcb);
            pcb = PROC1_$CURRENT_PCB;
        }
    }

    PROC1_$DISPATCH_INT2(pcb);

    ENABLE_INTERRUPTS(sr);
}
