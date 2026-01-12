#include "cal.h"

// Converts a 48-bit clock value to seconds.
// Clock ticks are 4 microseconds each, so divide by 250,000 (0x3D09 after >>4).
//
// The algorithm:
// 1. Right-shift the 48-bit value by 4 bits (divide by 16)
// 2. Divide by 0x3D09 (15625) using 32-bit/16-bit division
//
// 250,000 / 16 = 15,625 = 0x3D09
//
// This effectively computes: clock / 250,000
ulong CAL_$CLOCK_TO_SEC(clock_t *clock) {
    uint high_shifted;
    uint low32_shifted;
    ushort high_quotient;
    ushort low_quotient;
    ushort high_remainder;

    // Shift high part right by 4
    high_shifted = clock->high >> 4;

    // Divide high part by 0x3D09
    high_quotient = high_shifted / 0x3D09;
    high_remainder = high_shifted % 0x3D09;

    // Get lower 32 bits (high.low16 : low) shifted right by 4
    low32_shifted = *(uint *)((char *)&clock->high + 2) >> 4;

    // Combine remainder with low part and divide
    // CONCAT22(remainder, low_shifted.low) / 0x3D09
    low_quotient = (((uint)high_remainder << 16) | (low32_shifted & 0xFFFF)) / 0x3D09;

    // Return high_quotient : low_quotient
    return ((uint)high_quotient << 16) | low_quotient;
}
