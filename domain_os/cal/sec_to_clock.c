#include "cal.h"

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

    // Add high_sec * 3 to the upper 16 bits of high
    *(ushort *)clock_ret = *(ushort *)clock_ret + high_sec * 3;

    // Negate if original was negative (using 48-bit negation)
    if (is_negative) {
        // Negate the lower 32 bits (offset +2 in the struct)
        uint *low32 = (uint *)((char *)&clock_ret->high + 2);
        *low32 = -*low32;
        // Negate with extend the upper 16 bits
        *(ushort *)clock_ret = -(*(short *)clock_ret + (*low32 != 0 ? 1 : 0));
    }
}
