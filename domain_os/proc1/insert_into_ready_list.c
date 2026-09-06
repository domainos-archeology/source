/*
 * proc1_$insert_into_ready_list - LIFO priority-ordered ready list insertion
 *
 * Inserts a PCB into the ready list in the correct position.
 * The ready list is ordered by:
 *   1. resource_locks_held (descending - higher values first)
 *   2. state (descending - higher values first)
 *
 * Uses LIFO ordering within the same priority level: inserts BEFORE
 * entries with equal resource_locks_held and state, giving the newly
 * inserted process higher precedence.
 *
 * Contrast with proc1_$add_ready_body which inserts AFTER equal-priority
 * entries (FIFO/round-robin fairness).
 *
 * Parameters:
 *   pcb - Process to insert (passed in A1 on m68k)
 *
 * Original address: 0x00e20844
 */

#include "proc1/proc1_internal.h"

void proc1_$insert_into_ready_list(proc1_t *pcb)
{
    proc1_t *pos;
    proc1_t *prev;
    uint32_t locks = pcb->resource_locks_held;
    uint16_t state = pcb->state;

    /* Find insertion position - walk from head of ready list */
    pos = PROC1_$READY_PCB;

    /*
     * Walk the list while:
     *   - pcb's locks <= current position's locks AND
     *   - (locks are different OR pcb's state < current's state)
     *
     * List is ordered by locks descending, then state descending.
     * We skip past higher-priority entries (more locks or higher state)
     * and insert before the first entry with equal or lower priority.
     *
     * LIFO: bcs in assembly means "branch if unsigned lower" - we
     * continue past entries with strictly higher state, but STOP at
     * entries with equal state (inserting before them).
     */
    while (locks <= pos->resource_locks_held) {
        if (locks != pos->resource_locks_held) {
            /* pcb has fewer locks, keep walking */
            pos = pos->nextp;
            continue;
        }
        /* Equal locks - compare state (LIFO: stop at equal) */
        if (state >= pos->state) {
            /* pcb has equal or higher state, insert here */
            break;
        }
        pos = pos->nextp;
    }

    /* Insert pcb before pos */
    pcb->nextp = pos;
    prev = pos->prevp;
    pcb->prevp = prev;
    pos->prevp = pcb;
    prev->nextp = pcb;

    PROC1_$READY_COUNT++;
}
