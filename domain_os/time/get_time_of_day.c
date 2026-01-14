/*
 * TIME_$GET_TIME_OF_DAY - Get current time of day
 *
 * Returns seconds and microseconds since epoch in Unix timeval format.
 *
 * Parameters:
 *   tv - Pointer to receive timeval (tv[0]=seconds, tv[1]=microseconds)
 *
 * Original address: 0x00e2b06a
 *
 * The function reads the hardware timer, computes elapsed microseconds,
 * and adds them to TIME_$CURRENT_TIME and TIME_$CURRENT_USEC.
 * If microseconds overflow 1,000,000, it increments seconds.
 */

#include "time/time_internal.h"
#include "arch/m68k/arch.h"

void TIME_$GET_TIME_OF_DAY(uint32_t *tv)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIME_TIMER_BASE;
    uint16_t saved_sr;
    uint32_t seconds;
    uint32_t usecs;
    uint16_t ticks;
    uint16_t timer_val;

    /* Disable interrupts */
    GET_SR(saved_sr);
    SET_SR(saved_sr | SR_IPL_DISABLE_ALL);

    seconds = TIME_$CURRENT_TIME;

    /*
     * Read real-time timer using movep equivalent
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
                seconds++;  /* Carry means second rolled over */
            }
            ticks += TIME_$CURRENT_TICK;
        }
    }

    /* Clamp ticks to not exceed CURRENT_TICK */
    if ((int16_t)ticks > (int16_t)TIME_$CURRENT_TICK) {
        ticks = TIME_$CURRENT_TICK;
    }

    /*
     * Convert ticks to microseconds and add to stored usec value.
     * Each tick is 4 microseconds (250,000 ticks/sec).
     */
    usecs = TIME_$CURRENT_USEC + ((uint32_t)ticks * 4);

    /* If microseconds >= 1,000,000, roll over to next second */
    if (usecs >= 1000000) {
        seconds++;
        usecs -= 1000000;
    }

    /* Restore interrupts */
    SET_SR(saved_sr);

    /* Return result */
    tv[0] = seconds;
    tv[1] = usecs;
}
