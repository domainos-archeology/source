/*
 * pacct_$clock_to_comp - Convert clock value to compressed format
 *
 * Converts a 48-bit clock_t value to compressed format by:
 *   1. Dividing by 0x1047 (4167 decimal - the timer tick constant)
 *   2. Compressing the result using pacct_$compress
 *
 * The division is done in parts:
 *   - First divide the high 32 bits, getting a 16-bit quotient
 *   - Use the remainder combined with the low 16 bits for another divide
 *   - Combine the quotients and compress
 *
 * Original address: 0x00E5AA28
 * Size: 116 bytes
 *
 * Note: The original uses M$OIU$WLW and M$DIU$LLW for the division.
 * We emulate this behavior here for portability.
 */

#include "pacct_internal.h"

/* Timer tick constant - same as TIME_INITIAL_TICK */
#define PACCT_TICK_DIVISOR  0x1047

comp_t pacct_$clock_to_comp(clock_t *clock)
{
    uint32_t high_val;
    uint16_t low_val;
    uint32_t quot_high;
    uint32_t quot_low;
    uint32_t combined;

    high_val = clock->high;
    low_val = clock->low;

    /*
     * Divide the high 32 bits by 0x1047
     * M$OIU$WLW returns high 16 bits of (high_val * multiplier)
     * M$DIU$LLW returns quotient of (dividend / divisor)
     */

    /* Get partial quotient from high word division */
    quot_high = M$OIU$WLW(high_val, PACCT_TICK_DIVISOR);

    /* Combine partial quotient (shifted left 16) with low word, then divide */
    quot_low = M$DIU$LLW(((uint32_t)quot_high << 16) + (uint32_t)low_val,
                         PACCT_TICK_DIVISOR);

    /* Divide original high value and combine with quot_low */
    combined = (M$DIU$LLW(high_val, PACCT_TICK_DIVISOR) << 16) + quot_low;

    /* Compress the combined result */
    return pacct_$compress(combined);
}
