/*
 * PROC1_$VT_INT - Virtual-timer expiry: fold the timer into CPU time
 * Original address: 0x00e1491e (62 bytes)
 *
 * Frame: (0x8,A6) cpu_time_out (a 6-byte clock).  Called from
 * TIME_$VT_INT (0x00E163F4) at interrupt level.
 * Locals: (-0x8,A6) a 6-byte clock {0, vtimer}.
 *
 * 0x00E1491E  link.w A6,-0xc / pea (A2)
 * 0x00E14924  A2 = PROC1_$CURRENT_PCB (0xE1EAC8)
 * 0x00E1492A  clr.l (-0x8,A6); (-0x4,A6) = (0x48,A2)  delta = {0, vtimer}
 * 0x00E14934  ADD48(&pcb->cpu_total, &delta)          (no cleanup)
 * 0x00E14942  (0x48,A2) = 0                           vtimer = 0
 * 0x00E14946  A0 = cpu_time_out; (A0) = (0x4c,A2); (0x4,A0) = (0x50,A2)
 * 0x00E14954  movea.l (-0x10,A6),A2 / unlk / rts
 *
 * Parameters:
 *   cpu_time_out - receives the process's CPU clock after the fold
 */

#include "proc1/proc1_internal.h"
#include "cal/cal.h"

void PROC1_$VT_INT(clock_t *cpu_time_out)
{
    proc1_t *pcb;               /* A2 */
    clock_t delta;              /* (-0x8,A6) */

    /* 0x00E14924 */
    pcb = PROC1_$CURRENT_PCB;

    /* 0x00E1492A / 0x00E1492E */
    delta.high = 0;
    delta.low = (uint16_t)pcb->vtimer;

    /* 0x00E14934..0x00E1493C */
    ADD48((clock_t *)&pcb->cpu_total, &delta);

    /* 0x00E14942 */
    pcb->vtimer = 0;

    /* 0x00E14946..0x00E1494E */
    cpu_time_out->high = pcb->cpu_total;
    cpu_time_out->low = pcb->cpu_usage;
}
