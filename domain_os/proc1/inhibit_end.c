/*
 * PROC1_$INHIBIT_END - End an inhibit region
 *
 * Decrements the inhibit counter. When it reaches zero:
 * - Clears the inhibited flag
 * - Reorders in ready list if needed
 * - Handles any deferred operations
 * - May cause a context switch
 *
 * Must be paired with PROC1_$INHIBIT_BEGIN.
 *
 * Original address: 0x00e20ea2
 */

#include "proc1/proc1_internal.h"

void PROC1_$INHIBIT_END(void)
{
    proc1_t *pcb = PROC1_$CURRENT_PCB;

    /* Decrement inhibit counter */
    pcb->inh_count--;

    if (pcb->inh_count != 0) {
        /* Still inhibited, nothing more to do */
        return;
    }

    /* Inhibit count reached zero - clear inhibited flag */
    pcb->resource_locks_held &= ~0x01;

    /* TODO: Need to disable interrupts here (ori #0x700,SR) */

    /* Reorder in ready list since our state changed */
    proc1_$reorder_if_needed(pcb);

    /* If no locks held, handle deferred operations */
    if (pcb->resource_locks_held == 0) {
        uint8_t flags = pcb->pri_max;

        /* Clear bit 4 (0x10) - deferred flag */
        pcb->pri_max = flags & 0xEF;

        /* If bit 4 was set, do deferred ready list manipulation */
        if ((flags & 0x10) != 0) {
            proc1_$remove_from_ready_list(pcb);
            FUN_00e20824();
            /* pcb pointer may have changed - need to re-fetch */
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
