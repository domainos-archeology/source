/*
 * TIME_$VT_TIMER - Read virtual timer
 *
 * Returns the current virtual timer value from the hardware timer.
 * If an interrupt is pending or we're in the VT interrupt handler,
 * returns 0 instead.
 *
 * Original address: 0x00e2af6c
 *
 * Assembly:
 *   00e2af6c    lea (0xffac00).l,A0
 *   00e2af72    movep.w (0x9,A0),D0w      ; Read VT timer (bytes at 0x09, 0x0B)
 *   00e2af76    btst.b #0x1,(0x3,A0)      ; Check VT interrupt pending
 *   00e2af7c    bne.b 0x00e2af86         ; If set, return 0
 *   00e2af7e    tst.b (0x00e2af6a).l     ; Check IN_VT_INT flag
 *   00e2af84    beq.b 0x00e2af88         ; If clear, return timer value
 *   00e2af86    clr.w D0w                 ; Return 0
 *   00e2af88    rts
 */

#include "time/time_internal.h"

/*
 * Hardware timer access
 *
 * The M68K movep instruction reads/writes alternating bytes:
 * movep.w (0x9,A0),D0w reads bytes at offsets 0x09 and 0x0B
 */

uint16_t TIME_$VT_TIMER(void)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIME_TIMER_BASE;
    uint16_t value;

    /*
     * Read virtual timer using movep equivalent:
     * High byte from offset 0x09, low byte from offset 0x0B
     */
    value = ((uint16_t)timer_base[TIME_TIMER_VT_HI] << 8) |
             (uint16_t)timer_base[TIME_TIMER_VT_LO];

    /* Return 0 if VT interrupt is pending or we're in the handler */
    if ((timer_base[TIME_TIMER_CTRL] & TIME_CTRL_VT_INT) != 0) {
        return 0;
    }
    if (IN_VT_INT != 0) {
        return 0;
    }

    return value;
}
