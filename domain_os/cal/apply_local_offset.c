#include "cal.h"

// Applies the local timezone offset to a clock value.
// Converts the timezone's UTC delta (in minutes) to seconds,
// then to clock ticks, and adds it to the provided clock.
void CAL_$APPLY_LOCAL_OFFSET(clock_t *clock) {
    uint off_seconds;
    clock_t off_clock;

    off_seconds = CAL_$TIMEZONE.utc_delta * 60;
    CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock);
    ADD48(clock, &off_clock);
}
