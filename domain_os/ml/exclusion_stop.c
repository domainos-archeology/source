/*
 * ML_$EXCLUSION_STOP - Leave an exclusion region
 *
 * Leaves an exclusion region, waking any waiting processes if there
 * are waiters. May trigger rescheduling.
 *
 * The original code shares the exit path with ML_$UNLOCK.
 *
 * Original address: 0x00E20E7E
 */

#include "ml/ml_internal.h"

void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)
{
    proc1_t *pcb;
    int16_t old_state;
    uint8_t pri_flags;

    old_state = excl->f5;
    excl->f5--;

    pcb = PROC1_$CURRENT_PCB;

    uint16_t sr;

    if (old_state >= 1) {
        /*
         * There were waiters (state was positive before decrement).
         * Wake them up by advancing the event count.
         */
        DISABLE_INTERRUPTS(sr);

        ADVANCE_INT((ec_$eventcount_t *)excl);

        /* Decrement inhibit count */
        pcb->pad_5a--;

        if (pcb->pad_5a != 0) {
            ENABLE_INTERRUPTS(sr);
            return;
        }
    } else {
        /* No waiters - just decrement inhibit count */
        pcb->pad_5a--;

        if (pcb->pad_5a != 0) {
            return;
        }
    }

    /*
     * Inhibit count reached zero - need to handle deferred operations.
     * This is the common exit path shared with ML_$UNLOCK.
     */

    /* Clear "has locks" flag */
    *((uint8_t *)&pcb->resource_locks_held + 3) &= ~0x01;

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
