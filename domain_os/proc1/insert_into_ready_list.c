/*
 * proc1_$insert_into_ready_list - Insert process into ready list
 *
 * Inserts a PCB into the ready list in the correct position.
 * The ready list is ordered by:
 *   1. resource_locks_held (descending - higher values first)
 *   2. state (ascending - lower values first)
 *
 * This ordering ensures that:
 *   - Processes holding more locks run first (to release them quickly)
 *   - Among processes with equal locks, lower state runs first
 *
 * Parameters:
 *   pcb - Process to insert (passed in A1 on m68k)
 *
 * Original address: 0x00e20844
 */

#include "proc1.h"

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
     * This finds the first position where pcb should be inserted
     * before the current node.
     */
    while (locks <= pos->resource_locks_held) {
        if (locks != pos->resource_locks_held) {
            /* pcb has fewer locks, keep walking */
            pos = pos->nextp;
            continue;
        }
        /* Equal locks - compare state */
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
