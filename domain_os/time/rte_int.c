/*
 * TIME_$RTE_INT - Real-time timer interrupt handler
 *
 * Called when the real-time timer fires. Scans the RTE queue
 * and executes any callbacks whose time has come.
 *
 * Original address: 0x00e163a6
 *
 * Assembly:
 *   00e163a6    link.w A6,-0x10
 *   00e163aa    pea (A5)
 *   00e163ac    lea (0xe29198).l,A5      ; Base of queue area
 *   00e163b2    pea (-0xc,A6)
 *   00e163b6    jsr 0x00e2b026.l         ; TIME_$ABS_CLOCK
 *   00e163bc    addq.w #0x4,SP
 *   00e163be    pea (-0x4,A6)
 *   00e163c2    pea (-0xc,A6)
 *   00e163c6    pea (0x1608,A5)          ; &TIME_$RTEQ
 *   00e163ca    jsr 0x00e16e94.l         ; TIME_$Q_SCAN_QUEUE
 *   00e163d0    clr.b (0x00e2af6b).l     ; IN_RT_INT = 0
 *   00e163d6    movea.l #0x0,A0
 *   00e163dc    movea.l (-0x14,A6),A5
 *   00e163e0    unlk A6
 *   00e163e2    rts
 */

#include "time.h"

void TIME_$RTE_INT(void)
{
    clock_t now;
    uint32_t arg;  /* Unused argument for scan */

    /* Get current absolute time */
    TIME_$ABS_CLOCK(&now);

    /* Scan the RTE queue and fire expired callbacks */
    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &arg);

    /* Clear interrupt-in-progress flag */
    IN_RT_INT = 0;
}
