/*
 * TIME_$WRT_TIMER - Write real-time timer
 *
 * Writes a value to the hardware real-time timer.
 * Used to schedule the next timer interrupt.
 *
 * Parameters:
 *   value - Timer countdown value
 *
 * Original address: 0x00e2afa0
 *
 * The M68K movep instruction writes alternating bytes:
 * movep.w D0,(0x5,A0) writes to offsets 0x05 and 0x07
 */

#include "time.h"

void TIME_$WRT_TIMER(uint16_t value)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIME_TIMER_BASE;

    /*
     * Write timer value using movep equivalent:
     * High byte to offset 0x05, low byte to offset 0x07
     */
    timer_base[TIME_TIMER_RTE_HI] = (uint8_t)(value >> 8);
    timer_base[TIME_TIMER_RTE_LO] = (uint8_t)(value & 0xFF);
}
