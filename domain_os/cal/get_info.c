#include "cal.h"

// Copies the current timezone information to the caller's buffer.
// Copies 12 bytes (utc_delta, tz_name, drift).
void CAL_$GET_INFO(cal_$timezone_rec_t *info) {
    *info = CAL_$TIMEZONE;
}
