/*
 * PROC1_$REMOVE_READY - Take a process off the ready list (stack gate)
 * Original address: 0x00e206d2 (4 bytes)
 *
 * 0x00E206D2  movea.l (0x4,SP),A1            A1 = pcb (argument 1)
 *             ... falls through into proc1_$remove_from_ready_list (0x00E206D6)
 *
 * Callers push the PCB (`pea (A2)' PROC1_$TRY_TO_SUSPEND 0x00E14742,
 * `pea (A3)' PROC1_$TS_END_CALLBACK 0x00E14AD2); the assembly callers `bsr'
 * the body with A1 loaded.  On m68k both live in proc1/sau2/ready_list.s;
 * this C gate is the equivalent call for other targets.
 */

#include "proc1/proc1_internal.h"

#if !defined(ARCH_M68K)

void PROC1_$REMOVE_READY(proc1_t *pcb)
{
    /* 0x00E206D2 -> 0x00E206D6 */
    proc1_$remove_from_ready_list(pcb);
}

#endif /* !ARCH_M68K */
