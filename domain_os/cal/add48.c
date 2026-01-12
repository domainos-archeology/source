#include "cal.h"

// 48-bit addition: dst += src
// Adds two 48-bit clock values, propagating carry from low to high.
void ADD48(clock_t *dst, clock_t *src) {
    ushort dst_low = dst->low;
    ushort src_low = src->low;

    dst->low = dst_low + src_low;
    dst->high = dst->high + src->high + (dst->low < dst_low ? 1 : 0);
}
