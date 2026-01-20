/*
 * TIMER_$INIT - Initialize hardware timers
 *
 * Sets up the hardware timer interrupt vector and initializes all timers
 * with their starting values. This is called during system initialization.
 *
 * Timer configuration:
 *   - Timer 1 (RTE): Set to 0x1046 (initial tick count for real-time events)
 *   - Timer 2 (VT):  Set to 0xFFFF (max value, virtual timer disabled initially)
 *   - Timer 3 (aux): Set to 0xFFFF (max value, auxiliary timer disabled)
 *
 * Control register sequence:
 *   The function writes a specific sequence to the timer control registers
 *   at 0xFFAC01 and 0xFFAC03 to initialize the hardware:
 *     1. 0xE0 to 0xFFAC03
 *     2. 0xE1 to 0xFFAC01
 *     3. 0xE1 to 0xFFAC03
 *     4. 0xE0 to 0xFFAC01
 *
 * Original address: 0x00e16340
 *
 * Assembly:
 *   00e16340    link.w A6,-0xc
 *   00e16344    jsr TIME_$SET_VECTOR
 *   00e1634a    pea (0x50,PC)                ; Push addr of 0x1046 value
 *   00e1634e    pea (0x54,PC)                ; Push addr of timer index 1
 *   00e16352    jsr TIME_$WRT_TIMER          ; timer[1] = 0x1046
 *   00e16358    addq.w #0x8,SP
 *   00e1635a    pea (0x46,PC)                ; Push addr of 0xFFFF value
 *   00e1635e    pea (0x40,PC)                ; Push addr of timer index 2
 *   00e16362    jsr TIME_$WRT_TIMER          ; timer[2] = 0xFFFF
 *   00e16368    addq.w #0x8,SP
 *   00e1636a    pea (0x36,PC)                ; Push addr of 0xFFFF value
 *   00e1636e    pea (0x2e,PC)                ; Push addr of timer index 3
 *   00e16372    jsr TIME_$WRT_TIMER          ; timer[3] = 0xFFFF
 *   00e16378    movea.l #0xffac00,A0
 *   00e1637e    move.b #0xE0,(0x3,A0)        ; 0xFFAC03 = 0xE0
 *   00e16384    move.b #0xE1,(0x1,A0)        ; 0xFFAC01 = 0xE1
 *   00e1638a    move.b #0xE1,(0x3,A0)        ; 0xFFAC03 = 0xE1
 *   00e16390    move.b #0xE0,(0x1,A0)        ; 0xFFAC01 = 0xE0
 *   00e16396    clr.l D0
 *   00e16398    unlk A6
 *   00e1639a    rts
 *
 * Note: The decompiler shows only 2 control register writes, but the assembly
 *       clearly shows 4 writes in a specific sequence.
 */

#include "timer/timer.h"
#include "time/time.h"

/* Timer control register values */
#define TIMER_CTRL_VAL_E0   0xE0
#define TIMER_CTRL_VAL_E1   0xE1

/* Initial timer values (from ROM data at 0xe1639c-0xe163a5) */
#define TIMER_INIT_RTE_VALUE    0x1046  /* Initial RTE tick count */
#define TIMER_INIT_MAX_VALUE    0xFFFF  /* Max timer value (disabled) */

/* Timer indices */
#define TIMER_INDEX_RTE     1   /* Real-time event timer */
#define TIMER_INDEX_VT      2   /* Virtual timer */
#define TIMER_INDEX_AUX     3   /* Auxiliary timer */

int32_t TIMER_$INIT(void)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIMER_BASE_ADDR;

    /* Timer parameters - stored as static const to match original ROM layout */
    static const uint16_t timer_index_rte = TIMER_INDEX_RTE;
    static const uint16_t timer_index_vt  = TIMER_INDEX_VT;
    static const uint16_t timer_index_aux = TIMER_INDEX_AUX;
    static const uint16_t init_rte_value  = TIMER_INIT_RTE_VALUE;
    static const uint16_t init_max_value  = TIMER_INIT_MAX_VALUE;

    /* Step 1: Set up timer interrupt vector */
    TIME_$SET_VECTOR();

    /* Step 2: Initialize timer 1 (RTE) with initial tick count */
    TIME_$WRT_TIMER((uint16_t *)&timer_index_rte, (uint16_t *)&init_rte_value);

    /* Step 3: Initialize timer 2 (VT) with max value (disabled) */
    TIME_$WRT_TIMER((uint16_t *)&timer_index_vt, (uint16_t *)&init_max_value);

    /* Step 4: Initialize timer 3 (aux) with max value (disabled) */
    TIME_$WRT_TIMER((uint16_t *)&timer_index_aux, (uint16_t *)&init_max_value);

    /*
     * Step 5: Timer control register initialization sequence
     *
     * This sequence programs the timer hardware control registers.
     * The specific pattern (E0, E1, E1, E0) appears to:
     * - Reset/initialize the timer hardware
     * - Set appropriate operating modes
     * - Clear any pending interrupts
     *
     * Control bits (0xE0 = 0b11100000, 0xE1 = 0b11100001):
     * - Bit 0 toggles between writes
     * - Upper bits set timer operating mode
     */
    timer_base[TIMER_CTRL_OFFSET] = TIMER_CTRL_VAL_E0;  /* 0xFFAC03 = 0xE0 */
    timer_base[0x01]              = TIMER_CTRL_VAL_E1;  /* 0xFFAC01 = 0xE1 */
    timer_base[TIMER_CTRL_OFFSET] = TIMER_CTRL_VAL_E1;  /* 0xFFAC03 = 0xE1 */
    timer_base[0x01]              = TIMER_CTRL_VAL_E0;  /* 0xFFAC01 = 0xE0 */

    return 0;
}
