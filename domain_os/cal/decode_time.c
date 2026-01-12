#include "cal.h"

// Days per month lookup table (at 0x00e817ac)
// Used by DECODE_TIME to convert day-of-year to month/day
static short days_per_month[12] = {
    31,  // January
    28,  // February (non-leap year)
    31,  // March
    30,  // April
    31,  // May
    30,  // June
    31,  // July
    31,  // August
    30,  // September
    31,  // October
    30,  // November
    31   // December
};

// Decodes a 48-bit clock value into a time record.
//
// time_rec format (array of 6 shorts):
//   [0] = year
//   [1] = month (1-12)
//   [2] = day (1-31)
//   [3] = hour (0-23)
//   [4] = minute (0-59)
//   [5] = second (0-59)
//
// Domain/OS epoch is January 1, 1980.
void CAL_$DECODE_TIME(clock_t *clock, short *time_rec) {
    short local_days[12];
    ulong total_seconds;
    ulong remaining;
    short days_in_year;
    int day_of_year;
    short month;
    short cumulative_days;
    int i;

    // Copy days_per_month to local array (allows modification for leap year)
    for (i = 0; i < 12; i++) {
        local_days[i] = days_per_month[i];
    }

    // Convert clock to seconds
    total_seconds = CAL_$CLOCK_TO_SEC(clock);

    // Extract seconds (mod 60)
    time_rec[5] = M$OIU$WLW(total_seconds, 60);
    remaining = M$DIU$LLW(total_seconds, 60);

    // Extract minutes (mod 60)
    time_rec[4] = M$OIU$WLW(remaining, 60);
    remaining = M$DIU$LLW(remaining, 60);

    // Extract hours (mod 24)
    time_rec[3] = M$OIU$WLW(remaining, 24);
    remaining = M$DIU$LLW(remaining, 24);

    // remaining is now days since epoch
    // Epoch is January 1, 1980
    days_in_year = 366;  // 1980 is a leap year
    day_of_year = remaining + 1;
    time_rec[0] = 1980;

    // Find the year
    while (days_in_year < day_of_year) {
        day_of_year -= days_in_year;
        time_rec[0] = time_rec[0] + 1;

        // Check if next year is a leap year (divisible by 4)
        // Note: This simplified check works for years 1980-2099
        if (time_rec[0] % 4 == 0) {
            days_in_year = 366;
        } else {
            days_in_year = 365;
        }
    }

    // Update February days for leap year
    if (time_rec[0] % 4 == 0) {
        local_days[1] = 29;  // February has 29 days in leap year
    }

    // Find the month
    cumulative_days = 0;
    month = 1;
    for (i = 0; i < 12; i++) {
        if (day_of_year <= cumulative_days + local_days[i]) {
            break;
        }
        cumulative_days += local_days[i];
        month++;
    }

    time_rec[1] = month;
    time_rec[2] = (short)day_of_year - cumulative_days;
}
