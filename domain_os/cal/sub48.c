#include "cal.h"

// 48-bit subtraction: dst -= src
// Subtracts two 48-bit clock values, propagating borrow from low to high.
void SUB48(clock_t *dst, clock_t *src) {
    ushort dst_low = dst->low;
    ushort src_low = src->low;

    dst->low = dst_low - src_low;
    dst->high = dst->high - src->high - (dst_low < src_low ? 1 : 0);
}
