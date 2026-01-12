#include "cal.h"

// Calculates the day of the week for a given date using a variant
// of Zeller's congruence or similar algorithm.
//
// Returns: 0=Sunday, 1=Monday, ..., 6=Saturday
//
// The algorithm:
// 1. Adjust year backwards by 1 if month < 3 (Jan/Feb treated as months 13/14 of prior year)
// 2. Compute century correction terms
// 3. Use magic multiplier 0x99 (153) for month offset
// 4. Take result mod 7
short CAL_$WEEKDAY(short *year, short *month, short *day) {
    short y;
    short m;
    short adjusted_month;
    short sum;
    short result;

    // Adjust year if January or February
    if (*month < 3) {
        y = *year - 1;
    } else {
        y = *year;
    }

    // Compute base sum with leap year and century corrections
    // y/4 gives leap years, -y/100 removes century non-leap years, +y/400 adds back 400-year leap years
    sum = (y + ((y < 0 ? y + 3 : y) >> 2) + 1) - (y / 100) + (y / 400);

    // Compute month offset using formula: ((month + 9) mod 12) * 153 + 2) / 5
    // This maps months to their day-of-week offset
    adjusted_month = M$OIS$WLW(*month + 9, 12);
    sum = sum + (short)(((short)(adjusted_month * 0x99) + 2) / 5);

    // Add day and compute mod 7
    result = M$OIS$WLW((int)*day + (int)sum + 1, 7);

    // The switch statement in the original just ensures result is 0-6
    // (redundant given mod 7, but matches original code structure)
    switch (result) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 5;
        case 6: return 6;
        default: return result;
    }
}
