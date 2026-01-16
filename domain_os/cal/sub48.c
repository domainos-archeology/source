#include "cal.h"

// 48-bit subtraction: dst -= src
// Subtracts two 48-bit clock values, propagating borrow from low to high.
// Returns -1 if result is non-negative, 0 if result is negative.
// (Mirrors M68K 'spl' instruction behavior)
int8_t SUB48(clock_t *dst, clock_t *src) {
    ushort dst_low = dst->low;
    ushort src_low = src->low;

    dst->low = dst_low - src_low;
    dst->high = dst->high - src->high - (dst_low < src_low ? 1 : 0);

    // Return -1 (0xFF) if result is non-negative (high bit clear)
    // Return 0 if result is negative (high bit set)
    return ((int32_t)dst->high >= 0) ? -1 : 0;
}
