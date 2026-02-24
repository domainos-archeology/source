/*
 * PROC1_$ADD_READY - Add process to ready list (FIFO)
 *
 * Public interface to add a process to the ready list.
 * Calls proc1_$add_ready_body which uses FIFO ordering
 * within the same priority level (inserts after equal-priority
 * entries for round-robin fairness).
 *
 * On m68k, this is a 4-byte wrapper that loads the PCB pointer
 * from the stack into A1, then falls through to add_ready_body:
 *   movea.l (0x4,%sp), %a1
 *
 * Parameters:
 *   pcb - Process to add
 *
 * Original address: 0x00e20820
 */

#include "proc1/proc1_internal.h"

void PROC1_$ADD_READY(proc1_t *pcb)
{
    proc1_$add_ready_body(pcb);
}
