/*
 * PROC1_$GET_CPU_USAGE - CPU time (doubled) plus two PCB counters
 * Original address: 0x00e208aa (38 bytes)
 *
 * 0x00E208AA  bsr.b proc1_$get_current_cpu_time   (0x00E208D0; D1:D0)
 * 0x00E208AC  lsl.w #1,D0 / roxl.l #1,D1          48-bit shift left by one
 * 0x00E208B0  movea.l (0x4,SP),A0                 A0 = clock (argument 1)
 * 0x00E208B4  move.l D1,(A0) / move.w D0,(0x4,A0)
 * 0x00E208BA  movea.l (-0x1df4,PC),A1             A1 = PROC1_$CURRENT_PCB
 * 0x00E208BE  movea.l (0x8,SP),A0                 argument 2
 * 0x00E208C2  move.l (0x60,A1),(A0)               *stat1 = pcb->field_60
 * 0x00E208C6  movea.l (0xc,SP),A0                 argument 3
 * 0x00E208CA  move.l (0x64,A1),(A0)               *stat2 = pcb->field_64
 * 0x00E208CE  rts
 *
 * The PCB is re-read from PROC1_$CURRENT_PCB after the helper returns
 * (with interrupts back in their prior state).  Callers: PROC2 at
 * 0x00E41D48, PACCT at 0x00E74716.
 */

#include "proc1/proc1_internal.h"

void PROC1_$GET_CPU_USAGE(clock_t *clock, uint32_t *stat1_ret, uint32_t *stat2_ret)
{
    proc1_t *pcb;
    uint32_t d1;
    uint16_t d0;

    /* 0x00E208AA */
    proc1_$get_current_cpu_time(&d1, &d0);

    /* 0x00E208AC: lsl.w #1,D0 (X = old bit 15) / roxl.l #1,D1 */
    d1 = (d1 << 1) | ((uint32_t)d0 >> 15);
    d0 = (uint16_t)(d0 << 1);

    /* 0x00E208B0..0x00E208B6 */
    clock->high = d1;
    clock->low = d0;

    /* 0x00E208BA..0x00E208CA */
    pcb = PROC1_$CURRENT_PCB;
    *stat1_ret = pcb->field_60;
    *stat2_ret = pcb->field_64;
}
