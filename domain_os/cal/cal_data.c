/*
 * cal/cal_data.c - Calendar subsystem global data
 *
 * This file contains the global data definitions for the CAL subsystem.
 */

#include "cal/cal_internal.h"

/* Timezone record at 0x00e7b030 */
cal_$timezone_rec_t CAL_$TIMEZONE;

/* Boot volume index - mirrors CAL_$TIMEZONE.boot_volx for backward compat */
int16_t CAL_$BOOT_VOLX;

/* Last valid time (high word of clock) at 0x00e7b03c */
uint CAL_$LAST_VALID_TIME;

/* Days per month lookup table */
short CAL_$DAYS_PER_MONTH[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 * The MSM5832 RTC registers are memory-mapped at fixed addresses and are
 * reached through the CAL_$RTC_* macros in cal/cal.h, so there is nothing
 * to define here.
 */
