/*
 * TIMER_$INIT - Program the hardware timers
 *
 * Installs the timer interrupt vector, loads the three counters through
 * TIME_$WRT_TIMER (real-time with the 0x1046 period, virtual and auxiliary
 * with 0xFFFF), then pokes the two control bytes.  A Pascal function whose
 * result (D0 = 0) nothing reads.
 *
 * Original address: 0x00e16340, 92 bytes (the first routine of the TIME_
 * module, map: I E16340 TIME_ size 7A8)
 *
 *   00e16344  jsr TIME_$SET_VECTOR
 *   00e1634a  pea (0x50,PC) -> 0xE1639C value 0x1046 ; pea (0x54,PC) -> 0xE163A4 index 1
 *   00e16352  jsr TIME_$WRT_TIMER / addq.w #8
 *   00e1635a  pea (0x46,PC) -> 0xE163A2 value 0xFFFF ; pea (0x40,PC) -> 0xE163A0 index 2
 *   00e16362  jsr TIME_$WRT_TIMER / addq.w #8
 *   00e1636a  pea (0x36,PC) -> 0xE163A2 value 0xFFFF ; pea (0x2e,PC) -> 0xE1639E index 3
 *   00e16372  jsr TIME_$WRT_TIMER              ; args reclaimed by unlk
 *   00e16378  movea.l #0xffac00,A0
 *   00e1637e  move.b #0xe0,(0x3,A0)
 *   00e16384  move.b #0xe1,(0x1,A0)
 *   00e1638a  move.b #0xe1,(0x3,A0)
 *   00e16390  move.b #0xe0,(0x1,A0)
 *   00e16396  clr.l D0
 *
 * The by-reference constants sit after the rts, `gsk read 0xE1639C 10`:
 *   00e1639c  10 46   timer_$c_rte_period   (0x1047 - 1)
 *   00e1639e  00 03   timer_$c_index_aux
 *   00e163a0  00 02   timer_$c_index_vt
 *   00e163a2  ff ff   timer_$c_disabled     (shared by the VT and aux loads)
 *   00e163a4  00 01   timer_$c_index_rte
 */

#include "timer/timer_internal.h"

static const uint16_t timer_$c_rte_period = 0x1046;    /* 0x00E1639C */
static const uint16_t timer_$c_index_aux = 3;          /* 0x00E1639E */
static const uint16_t timer_$c_index_vt = 2;           /* 0x00E163A0 */
static const uint16_t timer_$c_disabled = 0xFFFF;      /* 0x00E163A2 */
static const uint16_t timer_$c_index_rte = 1;          /* 0x00E163A4 */

int32_t TIMER_$INIT(void)
{
    /* 0x00E16344 */
    TIME_$SET_VECTOR();

    /* 0x00E1634A..0x00E16372 */
    TIME_$WRT_TIMER((uint16_t *)&timer_$c_index_rte, (uint16_t *)&timer_$c_rte_period);
    TIME_$WRT_TIMER((uint16_t *)&timer_$c_index_vt, (uint16_t *)&timer_$c_disabled);
    TIME_$WRT_TIMER((uint16_t *)&timer_$c_index_aux, (uint16_t *)&timer_$c_disabled);

    /* 0x00E16378..0x00E16390 */
    TIME_$TIMER_WRITE(TIME_TIMER_CTRL, 0xE0);
    TIME_$TIMER_WRITE(0x01, 0xE1);
    TIME_$TIMER_WRITE(TIME_TIMER_CTRL, 0xE1);
    TIME_$TIMER_WRITE(0x01, 0xE0);

    /* 0x00E16396 */
    return 0;
}
