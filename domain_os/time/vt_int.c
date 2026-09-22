/*
 * TIME_$VT_INT - Virtual-timer interrupt body
 *
 * Runs (through TIME_$DI_VT) when the virtual timer fires: asks PROC1 for
 * the current process's CPU time, scans that process's VT queue with it,
 * and clears IN_VT_INT.
 *
 * Original address: 0x00e163e4, 80 bytes
 *
 *   00e163e8  pea (A5) / lea (0xe29198).l,A5          ; TIME_ data segment
 *   00e163f0  pea (-0x8,A6) / jsr PROC1_$VT_INT       ; cpu time -> -0x8
 *   00e163fc  pea (-0xc,A6)                           ; status
 *   00e16400  pea (-0x8,A6)                           ; now
 *   00e16404  D0 = PROC1_$CURRENT * 12; lea (0,A5,D0w),A0; pea (0x12fc,A0)
 *             ; 0xE29198 + 0x12FC + cur*12 = &TIME_$VTQ[cur - 1]
 *   00e1641a  jsr TIME_$Q_SCAN_QUEUE                  ; args reclaimed by unlk
 *   00e16420  clr.b (0x00e2af6a).l                    ; IN_VT_INT = 0
 *   00e16426  movea.l #0x0,A0                         ; A0 = 0 for the DI caller
 *
 * Frame: -0x08 cpu time (clock_t), -0x0C status (never read).
 */

#include "time/time_internal.h"

void TIME_$VT_INT(void)
{
    clock_t cpu_time;       /* A6-0x8 */
    status_$t status;       /* A6-0xC */

    /* 0x00E163F0..0x00E163FA */
    PROC1_$VT_INT(&cpu_time);

    /* 0x00E163FC..0x00E1641A */
    TIME_$Q_SCAN_QUEUE(&TIME_$VTQ[PROC1_$CURRENT - 1], &cpu_time, &status);

    /* 0x00E16420 */
    IN_VT_INT = 0;

    /* 0x00E16426: movea.l #0x0,A0 - a result no C caller sees */
}
