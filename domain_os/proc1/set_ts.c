/*
 * PROC1_$SET_TS - Re-arm a process's timeslice timer
 * Original address: 0x00e14a08 (104 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8; the element is
 * PROC1_$TS_ELEM[pid].elem at (0x14,A5 + pid*0x1C) and the queue is
 * TIME_$VTQ[pid - 1] (`pea (-0xc,A0,D3w)' with A0 = 0xE2A4A0, D3 = pid*12).
 *
 * Frame: (0x8,A6) pcb, (0xC,A6) timeslice (word).
 * Locals: (-0xC,A6) a 6-byte clock {0, timeslice}, (-0x4,A6) status.
 *
 * 0x00E14A08  link.w A6,-0x10 / movem.l D2-D4/A2/A5,-(SP) / lea A5
 * 0x00E14A16  A2 = pcb; D1 = timeslice
 * 0x00E14A20  clr.l (-0xc,A6) / move.w D1,(-0x8,A6)     when = {0, timeslice}
 * 0x00E14A28  subq.l #2,SP                              Pascal result slot
 * 0x00E14A2A  pea (-0x4,A6)                             &status
 * 0x00E14A2E  D2 = pcb->mypid * 0x1C; pea (0x14,A5,D2)  &elem
 * 0x00E14A40  pea (0x4c,A2)                             &pcb->cpu_total (base)
 * 0x00E14A44  clr.w -(SP)                               qflags = 0
 * 0x00E14A46  pea (-0xc,A6)                             &when
 * 0x00E14A4A  D3 = pcb->mypid * 12; pea (-0xc,A0,D3)    &TIME_$VTQ[mypid-1]
 * 0x00E14A60  jsr TIME_$Q_REENTER_ELEM                  (no cleanup: unlk)
 * 0x00E14A66  movem.l / unlk / rts
 *
 * The queue's base time is the PCB's own CPU clock (cpu_total:cpu_usage),
 * so the element expires `timeslice' ticks of CPU time from now.  The
 * function result and the status are both discarded.
 *
 * Parameters:
 *   pcb       - the process
 *   timeslice - ticks of CPU time until PROC1_$TS_END_CALLBACK fires
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"

void PROC1_$SET_TS(proc1_t *pcb, int16_t timeslice)
{
    clock_t when;               /* (-0xC,A6) */
    status_$t status;           /* (-0x4,A6) */
    uint16_t pid;

    /* 0x00E14A20 / 0x00E14A24 */
    when.high = 0;
    when.low = (uint16_t)timeslice;

    /* 0x00E14A2E / 0x00E14A4A: both indices come from pcb->mypid */
    pid = pcb->mypid;

    /* 0x00E14A28..0x00E14A60 */
    TIME_$Q_REENTER_ELEM(&TIME_$VTQ[pid - 1], &when, 0,
                         (clock_t *)&pcb->cpu_total,
                         &PROC1_$TS_ELEM[pid].elem, &status);
}
