#include "cal/cal_internal.h"

// Converts seconds to a 48-bit clock value.
// Clock ticks are 4 microseconds each, so 250,000 ticks per second.
// 250,000 = 0x3D090 = 3 * 0x10000 + 0xD090
//
// The multiplication is done using partial products to handle
// the 32-bit x 18-bit multiply on 68010 (no 32x32 multiply).
//
// For input sec:
//   low_sec = sec & 0xFFFF
//   high_sec = sec >> 16
//
//   result.low = (low_sec * 0xD090) & 0xFFFF
//   result.high = high_sec * 0xD090 + low_sec * 3 + (low_sec * 0xD090) >> 16
//   result.high += high_sec * 3  (added to upper 16 bits)
void CAL_$SEC_TO_CLOCK(uint *sec, clock_t *clock_ret) {
    uint s;
    ushort high_sec;
    uint product_low;
    uint product_high;
    int is_negative;

    s = *sec;
    is_negative = (int)s < 0;
    if (is_negative) {
        s = -s;
    }

    // Multiply by 0x3D090 (250,000) using partial products
    // low_sec * 0xD090
    product_low = (s & 0xFFFF) * 0xD090;
    clock_ret->low = (ushort)product_low;

    high_sec = (ushort)(s >> 16);

    // Build high part: carry from low + low_sec*3 + high_sec*0xD090
    product_high = (product_low >> 16) + (s & 0xFFFF) * 3 + (uint)high_sec * 0xD090;
    clock_ret->high = product_high;

    // Add high_sec * 3 to the upper 16 bits of high.
    // Original: add.w to the first word of the (big-endian) clock_t, i.e.
    // bits 47..32 of the 48-bit value.  Expressed with shifts so the result
    // is the same on little-endian hosts.
    clock_ret->high = (clock_ret->high & 0xFFFF) |
                      ((uint)(ushort)((clock_ret->high >> 16) + high_sec * 3) << 16);

    // Negate if original was negative (using 48-bit negation)
    if (is_negative) {
        // Negate the lower 32 bits (bits 31..0: low 16 bits of `high`
        // followed by `low`; the original does a neg.l at struct offset +2)
        uint low32 = ((clock_ret->high & 0xFFFF) << 16) | clock_ret->low;
        low32 = -low32;
        clock_ret->high = (clock_ret->high & 0xFFFF0000) | (low32 >> 16);
        clock_ret->low = (ushort)low32;
        // Negate with extend the upper 16 bits (negx.w on the first word)
        clock_ret->high = (clock_ret->high & 0xFFFF) |
                          ((uint)(ushort)(-((short)(clock_ret->high >> 16) +
                                            (low32 != 0 ? 1 : 0))) << 16);
    }
}
