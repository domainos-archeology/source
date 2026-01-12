#include "cal.h"

// External functions
extern void CRASH_SYSTEM(status_$t *status);
extern void FUN_00e825f4(char *format, ...);
extern void TERM_$READ(void *param1, char *buffer, void *param3, status_$t *status);

// String constants (addresses from disassembly)
// These would be defined elsewhere in the actual system

// Verifies that the system clock is valid and not too far off from the last known time.
//
// Parameters:
//   max_allowed_delta - maximum allowed difference in TIME_$CLOCKH units
//   param_2 - passed to error message formatting
//   param_3 - if negative, prompt user interactively
//   status_ret - receives status code
//
// Returns:
//   0xFF (true) if calendar is valid or user accepts
//   0x00 (false) if calendar is invalid and user refused
//
// The function checks two conditions:
// 1. If time has gone backwards more than 229 units (~1 minute), show "calendar is more than a minute off"
// 2. If time has advanced more than max_allowed_delta, show "more than N days have elapsed"
char CAL_$VERIFY(int *max_allowed_delta, void *param_2, char *param_3, status_$t *status_ret) {
    cal_$timezone_rec_t tz;
    status_$t status;
    char input[8];
    int delta;

    *status_ret = status_$ok;

    // Read timezone from disk
    CAL_$READ_TIMEZONE(&tz, &status);

    // Clear drift correction
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    // Calculate time delta
    delta = TIME_$CLOCKH - CAL_$LAST_VALID_TIME;

    if (delta < -229) {
        // Time has gone backwards significantly
        // "The calendar is more than a minute off"
        FUN_00e825f4("  The calendar is more than a minute behind the last valid time.\n",
                     param_2, param_2);
    } else if (delta <= *max_allowed_delta) {
        // Time is within acceptable range
        return 0xFF;
    } else {
        // "More than N days have elapsed"
        FUN_00e825f4("  More than %a days have elapsed since last valid time.\n",
                     param_2, param_2);
    }

    // If param_3 is negative, prompt user interactively
    if (*param_3 < 0) {
        do {
            FUN_00e825f4("Do you want to run DOMAIN/OS with the current calendar setting? (Y/N) ",
                         param_2);
            TERM_$READ((void *)0xe684a4, input, (void *)0xe684a6, &status);

            if (input[0] == 'Y' || input[0] == 'y') {
                return 0xFF;
            }
        } while (input[0] != 'N' && input[0] != 'n');

        FUN_00e825f4("  Please set the calendar using the 'set_time' command.\n", param_2);
        *status_ret = status_$cal_refused;
    }

    return 0;
}
