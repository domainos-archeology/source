/*
 * TIME_$ABS_CLOCK - Get absolute clock value
 *
 * Returns the absolute 48-bit clock value (adjusted for drift).
 * Similar to TIME_$CLOCK but uses TIME_$CLOCKH/L instead of
 * TIME_$CURRENT_CLOCKH/L.
 *
 * Parameters:
 *   clock - Pointer to receive clock value
 *
 * Original address: 0x00e2b026
 *
 * Assembly:
 *   00e2b026    lea (0xffac00).l,A0
 *   00e2b02c    move SR,D1w
 *   00e2b02e    ori #0x700,SR            ; Disable interrupts
 *   00e2b032    movea.l (0xa0,PC),A1     ; Load TIME_$CLOCKH
 *   00e2b036    movep.w (0x5,A0),D0w     ; Read RTE timer
 *   00e2b03a    not.w D0w                ; Complement
 *   00e2b03c    add.w #0x1047,D0w        ; Add initial value
 *   00e2b040    cmp.w #0xfe3,D0w         ; Check if < 0xFE4
 *   00e2b044    bgt.b 0x00e2b008         ; Jump to clock.c code (shared)
 *   00e2b046    btst.b #0x0,(0x3,A0)     ; Check RTE interrupt pending
 *   00e2b04c    beq.b 0x00e2b056
 *   00e2b04e    add.w #0x1047,D0w        ; Add initial tick value again
 *   00e2b052    bcc.b 0x00e2b056
 *   00e2b054    addq.l #0x1,A1           ; Increment high word on carry
 *   00e2b056    add.w (0x88,PC),D0w      ; Add TIME_$CLOCKL
 *   00e2b05a    bcc.b 0x00e2b05e
 *   00e2b05c    addq.l #0x1,A1           ; Increment high on carry
 *   00e2b05e    move D1w,SR              ; Restore interrupts
 *   00e2b060    movea.l (0x4,SP),A0
 *   00e2b064    move.l A1,(A0)+          ; Store high word
 *   00e2b066    move.w D0w,(A0)          ; Store low word
 *   00e2b068    rts
 */

#include "time.h"
#include "arch/m68k/arch.h"

void TIME_$ABS_CLOCK(clock_t *clock)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIME_TIMER_BASE;
    uint16_t saved_sr;
    uint32_t high;
    uint16_t ticks;
    uint16_t timer_val;

    /* Disable interrupts */
    GET_SR(saved_sr);
    SET_SR(saved_sr | SR_IPL_DISABLE_ALL);

    high = TIME_$CLOCKH;

    /*
     * Read real-time timer using movep equivalent:
     * High byte from offset 0x05, low byte from offset 0x07
     */
    timer_val = ((uint16_t)timer_base[TIME_TIMER_RTE_HI] << 8) |
                 (uint16_t)timer_base[TIME_TIMER_RTE_LO];

    /* Complement and add initial value to get elapsed ticks */
    ticks = ~timer_val + TIME_INITIAL_TICK;

    /*
     * If ticks < 0xFE4 and RTE interrupt is pending,
     * the timer has wrapped - add another 0x1047
     */
    if (ticks < 0xFE4) {
        if ((timer_base[TIME_TIMER_CTRL] & TIME_CTRL_RTE_INT) != 0) {
            if ((uint16_t)(ticks + TIME_INITIAL_TICK) < ticks) {
                high++;  /* Carry occurred */
            }
            ticks += TIME_INITIAL_TICK;
        }
    }

    /* Add CLOCKL to get final low word */
    if ((uint16_t)(TIME_$CLOCKL + ticks) < ticks) {
        high++;  /* Carry occurred */
    }
    ticks += TIME_$CLOCKL;

    /* Restore interrupts */
    SET_SR(saved_sr);

    /* Return result */
    clock->high = high;
    clock->low = ticks;
}
