/*
 * cal/cal_data.c - Calendar subsystem global data
 *
 * This file contains the global data definitions for the CAL subsystem.
 */

#include "cal/cal.h"

/* Timezone record at 0x00e7b030 */
cal_$timezone_rec_t CAL_$TIMEZONE;

/* Boot volume index - mirrors CAL_$TIMEZONE.boot_volx for backward compat */
int16_t CAL_$BOOT_VOLX;

/* Last valid time (high word of clock) at 0x00e7b03c */
uint CAL_$LAST_VALID_TIME;

/* Days per month lookup table */
short CAL_$DAYS_PER_MONTH[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* Hardware clock registers - these are memory-mapped, so we can't really define them here */
/* On non-M68K platforms, these would need to be handled differently */
#ifndef M68K
volatile char CAL_$CONTROL_VIRTUAL_ADDR;
volatile char CAL_$WRITE_DATA_VIRTUAL_ADDR;
#endif
