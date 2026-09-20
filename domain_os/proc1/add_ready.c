/*
 * PROC1_$ADD_READY - Add a process to the ready list (stack-argument gate)
 * Original address: 0x00e20820 (4 bytes)
 *
 * 0x00E20820  movea.l (0x4,SP),A1            A1 = pcb (argument 1)
 *             ... falls through into proc1_$add_ready_body (0x00E20824)
 *
 * The gate exists so Pascal callers can push the PCB (PROC1_$RESUME
 * 0x00E147C8, PROC1_$TS_END_CALLBACK 0x00E14ADE, PROC1_$INIT 0x00E2F9C6);
 * the assembly callers `bsr' the body with A1 already loaded.  There is no
 * `rts' of its own: the body's rts at 0x00E2087A returns to the caller.
 *
 * On m68k the gate is the four bytes ahead of the body in
 * proc1/sau2/ready_list.s; this C body is the equivalent call for
 * other targets.
 */

#include "proc1/proc1_internal.h"

#if !defined(ARCH_M68K)

void PROC1_$ADD_READY(proc1_t *pcb)
{
    /* 0x00E20820 -> 0x00E20824 */
    proc1_$add_ready_body(pcb);
}

#endif /* !ARCH_M68K */
