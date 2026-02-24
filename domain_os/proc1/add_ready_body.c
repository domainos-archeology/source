/*
 * proc1_$add_ready_body - FIFO priority-ordered ready list insertion
 *
 * Inserts a PCB into the ready list ordered by:
 *   1. resource_locks_held (descending - higher values first)
 *   2. state (descending - higher values first)
 *
 * Uses FIFO ordering within the same priority level: inserts AFTER
 * entries with equal resource_locks_held and state. This ensures
 * round-robin fairness among processes at the same priority.
 *
 * Contrast with proc1_$insert_into_ready_list which uses LIFO
 * ordering (inserts BEFORE equal-priority entries, giving the
 * newly inserted process higher precedence).
 *
 * On m68k, the assembly version (sau2/add_ready_body.s) uses register
 * calling convention with A1 = PCB pointer. This C version provides
 * a portable implementation with standard calling convention.
 *
 * Parameters:
 *   pcb - Process to insert into ready list
 *
 * Original address: 0x00e20824
 */

#include "proc1/proc1_internal.h"

void proc1_$add_ready_body(proc1_t *pcb)
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
     *   - (locks are different OR pcb's state <= current's state)
     *
     * The key difference from proc1_$insert_into_ready_list is the
     * use of <= (not <) for state comparison - this causes insertion
     * AFTER equal-priority entries (FIFO) rather than BEFORE (LIFO).
     *
     * Assembly uses:
     *   cmp.w (0x52,A0),D0   ; compare state
     *   bls.b .Lnext         ; branch if lower or same (FIFO)
     * vs insert_into_ready_list:
     *   cmp.w (0x52,A0),D0
     *   bcs.b .Lnext         ; branch if strictly lower (LIFO)
     */
    while (locks <= pos->resource_locks_held) {
        if (locks != pos->resource_locks_held) {
            /* pcb has fewer locks, keep walking */
            pos = pos->nextp;
            continue;
        }
        /* Equal locks - compare state (FIFO: skip past equal) */
        if (state > pos->state) {
            /* pcb has strictly higher state, insert here */
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
