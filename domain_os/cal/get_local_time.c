#include "cal.h"

// Gets the current local time by:
// 1. Computing the timezone offset in clock ticks
// 2. Reading the system clock
// 3. Adding the timezone offset
// 4. Adding the drift correction
void CAL_$GET_LOCAL_TIME(clock_t *clock) {
    uint off_seconds;
    clock_t off_clock;

    off_seconds = CAL_$TIMEZONE.utc_delta * 60;
    CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock);
    TIME_$CLOCK(clock);
    ADD48(clock, &off_clock);
    ADD48(clock, &CAL_$TIMEZONE.drift);
}
