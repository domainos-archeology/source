/*
 * TIME_$SET_VECTOR - Install the timer interrupt handler
 *
 * Stores the address of the timer interrupt entry into exception vector
 * 0x78 / 4 = 30, the level-6 autovector.
 *
 * Original address: 0x00e2b102, 12 bytes (TIME_ASM segment, hand-written)
 *
 *   00e2b102  lea (0x2c,PC),A0                ; 0xE2B104 + 0x2C = 0xE2B130
 *   00e2b106  move.l A0,(0x00000078).l        ; ARCH_AUTOVECTOR(6)
 *   00e2b10c  rts
 *
 * The handler at 0x00E2B130 (TIME_$TIMER_HANDLER, 336 bytes up to the end
 * of TIME_ASM at 0x00E2B280) is hand-written assembly,
 * time/sau2/timer_handler.s.
 */

#include "time/time_internal.h"

#define TIME_TIMER_INT_LEVEL 6      /* vector 0x78 = 24 + 6 */

void TIME_$SET_VECTOR(void)
{
    /* 0x00E2B106 */
    ARCH_AUTOVECTOR(TIME_TIMER_INT_LEVEL) = (void *)&TIME_$TIMER_HANDLER;
}
