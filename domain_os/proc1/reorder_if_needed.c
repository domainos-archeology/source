/*
 * proc1_$reorder_if_needed - Reorder process in ready list if needed
 *
 * Checks if a process needs to move in the ready list due to
 * changes in resource_locks_held or state. If the process is
 * out of order relative to its neighbors, removes and re-inserts it.
 *
 * Parameters:
 *   pcb - Process to potentially reorder (passed in A1 on m68k)
 *
 * Original address: 0x00e207d8
 */

#include "proc1.h"

void proc1_$reorder_if_needed(proc1_t *pcb)
{
    uint32_t locks = pcb->resource_locks_held;
    uint32_t prev_locks, next_locks;

    /*
     * Check if we need to move toward the head (higher priority).
     * This happens if our prev has fewer locks, or equal locks
     * with higher state.
     */
    if (pcb != PROC1_$READY_PCB) {
        prev_locks = pcb->prevp->resource_locks_held;

        if (prev_locks < locks) {
            /* We have more locks than prev - need to move forward */
            proc1_$remove_from_ready_list(pcb);
            proc1_$insert_into_ready_list(pcb);
            return;
        }

        if (locks == prev_locks) {
            /* Equal locks - check state */
            if (pcb->prevp->state > pcb->state) {
                /* Prev has higher state - we should be before it */
                proc1_$remove_from_ready_list(pcb);
                proc1_$insert_into_ready_list(pcb);
                return;
            }
        }
    }

    /*
     * Check if we need to move toward the tail (lower priority).
     * This happens if our next has more locks, or equal locks
     * with lower state.
     */
    next_locks = pcb->nextp->resource_locks_held;

    if (locks < next_locks) {
        /* Next has more locks - need to move backward */
        proc1_$remove_from_ready_list(pcb);
        proc1_$insert_into_ready_list(pcb);
        return;
    }

    if (locks == next_locks && pcb->state > pcb->nextp->state) {
        /* Equal locks, we have higher state - need to move backward */
        proc1_$remove_from_ready_list(pcb);
        proc1_$insert_into_ready_list(pcb);
    }

    /* No reordering needed */
}
