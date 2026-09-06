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

/*
 * On m68k (SAU2) the wrapper is the 4-byte assembly stub in
 * sau2/add_ready_body.s that loads A1 from the stack and falls through
 * into proc1_$add_ready_body.  This C version is only used on other
 * architectures.
 */
#if !defined(ARCH_M68K)

void PROC1_$ADD_READY(proc1_t *pcb)
{
    proc1_$add_ready_body(pcb);
}

#endif /* !ARCH_M68K */
