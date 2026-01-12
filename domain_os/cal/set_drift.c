#include "cal.h"

// Sets the drift correction value in the global timezone record.
void CAL_$SET_DRIFT(clock_t *drift) {
    CAL_$TIMEZONE.drift.high = drift->high;
    CAL_$TIMEZONE.drift.low = drift->low;
}
