/*
 * pacct_$compress - Compress a value to comp_t format
 *
 * Compresses a 32-bit value to 16-bit comp_t format using base-8
 * floating point representation:
 *   bits 0-12:  13-bit mantissa (values 0-8191)
 *   bits 13-15: 3-bit exponent (multiply by 8^exp)
 *
 * Algorithm:
 *   1. While value > 0x1FFF (13 bits), shift right by 3 and increment exponent
 *   2. Track bit 2 before each shift for rounding
 *   3. If rounding bit was set and value+1 overflows 13 bits, shift again
 *   4. Pack exponent (shifted left 13) | mantissa
 *
 * This matches the Unix comp_t format used in process accounting.
 *
 * Original address: 0x00E5A9CA
 * Size: 94 bytes
 */

#include "pacct_internal.h"

comp_t pacct_$compress(uint32_t value)
{
    int8_t exponent = 0;
    int8_t round_up = 0;

    /* Shift right by 3 until value fits in 13 bits */
    while (value >= 0x2000) {   /* 0x2000 = 8192 = 2^13 */
        exponent++;
        /* Track bit 2 for rounding (will become bit 0 after shift) */
        round_up = (value & 0x4) ? -1 : 0;  /* sne instruction result */
        value >>= 3;
    }

    /* Apply rounding if needed */
    if (round_up < 0) {
        value++;
        /* Check if rounding caused overflow */
        if (value >= 0x2000) {
            exponent++;
            value >>= 3;
        }
    }

    /* Pack exponent and mantissa */
    return ((comp_t)(exponent & 0x7) << 13) | (comp_t)(value & 0x1FFF);
}
