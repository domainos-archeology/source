/*
 * TIME_$CLOCK - Get current clock value (raw)
 *
 * Returns the current 48-bit clock value. This reads the hardware
 * timer and combines it with the stored clock values.
 *
 * The hardware timer counts down from 0x1047. We complement it
 * and add an offset to get the elapsed ticks, then add to the
 * stored clock values.
 *
 * Parameters:
 *   clock - Pointer to receive clock value
 *
 * Original address: 0x00e2afd6
 *
 * Assembly:
 *   00e2afd6    lea (0xffac00).l,A0
 *   00e2afdc    move SR,D1w
 *   00e2afde    ori #0x700,SR            ; Disable interrupts
 *   00e2afe2    movea.l (0x100,PC),A1    ; Load TIME_$CURRENT_CLOCKH
 *   00e2afe6    clr.l D0
 *   00e2afe8    movep.w (0x5,A0),D0w     ; Read RTE timer
 *   00e2afec    not.w D0w                ; Complement
 *   00e2afee    add.w #0x1047,D0w        ; Add initial value
 *   00e2aff2    cmp.w #0xfe3,D0w         ; Check if < 0xFE4
 *   00e2aff6    bgt.b 0x00e2b008
 *   00e2aff8    btst.b #0x0,(0x3,A0)     ; Check RTE interrupt pending
 *   00e2affe    beq.b 0x00e2b008
 *   00e2b000    add.w (0xf6,PC),D0w      ; Add CURRENT_TICK
 *   00e2b004    bcc.b 0x00e2b008
 *   00e2b006    addq.l #0x1,A1           ; Increment high word on carry
 *   00e2b008    cmp.w (0xee,PC),D0w      ; Compare with CURRENT_TICK
 *   00e2b00c    ble.b 0x00e2b012
 *   00e2b00e    move.w (0xe8,PC),D0w     ; Clamp to CURRENT_TICK
 *   00e2b012    add.w (0xd4,PC),D0w      ; Add CURRENT_CLOCKL
 *   00e2b016    bcc.b 0x00e2b01a
 *   00e2b018    addq.l #0x1,A1           ; Increment high on carry
 *   00e2b01a    move D1w,SR              ; Restore interrupts
 *   00e2b01c    movea.l (0x4,SP),A0
 *   00e2b020    move.l A1,(A0)+          ; Store high word
 *   00e2b022    move.w D0w,(A0)          ; Store low word
 *   00e2b024    rts
 */

#include "time.h"
#include "arch/m68k/arch.h"

void TIME_$CLOCK(clock_t *clock)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIME_TIMER_BASE;
    uint16_t saved_sr;
    uint32_t high;
    uint16_t ticks;
    uint16_t timer_val;

    /* Disable interrupts */
    GET_SR(saved_sr);
    SET_SR(saved_sr | SR_IPL_DISABLE_ALL);

    high = TIME_$CURRENT_CLOCKH;

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
     * the timer has wrapped - add CURRENT_TICK
     */
    if (ticks < 0xFE4) {
        if ((timer_base[TIME_TIMER_CTRL] & TIME_CTRL_RTE_INT) != 0) {
            if ((uint16_t)(TIME_$CURRENT_TICK + ticks) < ticks) {
                high++;  /* Carry occurred */
            }
            ticks += TIME_$CURRENT_TICK;
        }
    }

    /* Clamp ticks to not exceed CURRENT_TICK */
    if ((int16_t)ticks > (int16_t)TIME_$CURRENT_TICK) {
        ticks = TIME_$CURRENT_TICK;
    }

    /* Add CURRENT_CLOCKL to get final low word */
    if ((uint16_t)(TIME_$CURRENT_CLOCKL + ticks) < ticks) {
        high++;  /* Carry occurred */
    }
    ticks += TIME_$CURRENT_CLOCKL;

    /* Restore interrupts */
    SET_SR(saved_sr);

    /* Return result */
    clock->high = high;
    clock->low = ticks;
}
