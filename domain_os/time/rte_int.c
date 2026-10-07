/*
 * TIME_$RTE_INT - Real-time timer interrupt body
 *
 * Runs (through the deferred-interrupt element TIME_$DI_RTE) when the
 * real-time timer fires: reads the absolute clock, scans TIME_$RTEQ for
 * expired elements, and clears the IN_RT_INT flag the interrupt stub set.
 *
 * Original address: 0x00e163a6, 62 bytes
 *
 *   00e163a6  link.w A6,-0x10
 *   00e163aa  pea (A5) / lea (0xe29198).l,A5   ; TIME_ data segment
 *   00e163b2  pea (-0xc,A6) / jsr TIME_$ABS_CLOCK
 *   00e163be  pea (-0x4,A6)                    ; status
 *   00e163c2  pea (-0xc,A6)                    ; now
 *   00e163c6  pea (0x1608,A5)                  ; TIME_$RTEQ (0xE2A7A0)
 *   00e163ca  jsr TIME_$Q_SCAN_QUEUE           ; args reclaimed by unlk
 *   00e163d0  clr.b (0x00e2af6b).l             ; IN_RT_INT = 0
 *   00e163d6  movea.l #0x0,A0                  ; A0 = 0 for the DI caller
 *   00e163dc  movea.l (-0x14,A6),A5 / unlk / rts
 *
 * Frame: -0x0C now (clock_t), -0x04 status (never read back).
 */

#include "time/time_internal.h"

void TIME_$RTE_INT(void)
{
    clock_t now;            /* A6-0xC */
    status_$t status;       /* A6-0x4 */

    /* 0x00E163B2..0x00E163BC */
    TIME_$ABS_CLOCK(&now);

    /* 0x00E163BE..0x00E163CA */
    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    /* 0x00E163D0 */
    IN_RT_INT = 0;

    /* 0x00E163D6: movea.l #0x0,A0 - the result the deferred-interrupt
     * dispatcher reads (di_loop: an eventcount to advance, or 0) */
    ARCH_RESULT_A0(NULL);
}
