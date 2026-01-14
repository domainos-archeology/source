/*
 * TIME_$WRT_VT_TIMER - Write virtual timer
 *
 * Writes a value to the hardware virtual timer.
 * Used to schedule the next virtual timer interrupt.
 *
 * Parameters:
 *   value - Timer countdown value
 *
 * Original address: 0x00e2af8a
 *
 * The M68K movep instruction writes alternating bytes:
 * movep.w D0,(0x9,A0) writes to offsets 0x09 and 0x0B
 */

#include "time/time_internal.h"

void TIME_$WRT_VT_TIMER(uint16_t value)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIME_TIMER_BASE;

    /*
     * Write timer value using movep equivalent:
     * High byte to offset 0x09, low byte to offset 0x0B
     */
    timer_base[TIME_TIMER_VT_HI] = (uint8_t)(value >> 8);
    timer_base[TIME_TIMER_VT_LO] = (uint8_t)(value & 0xFF);
}
