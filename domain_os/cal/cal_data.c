/*
 * cal/cal_data.c - Calendar subsystem global data
 *
 * This file contains the global data definitions for the CAL subsystem.
 */

#include "cal/cal_internal.h"

/*
 * The OS_CAL_WIRED segment (map: D E7B030 size 0x14), see cal/cal.h:
 *   0x00E7B030  CAL_$TIMEZONE         12 bytes
 *   0x00E7B03C  CAL_$LAST_VALID_TIME  longword
 *   0x00E7B040  CAL_$BOOT_VOLX        word
 */
cal_$timezone_rec_t CAL_$TIMEZONE;
uint CAL_$LAST_VALID_TIME;
int16_t CAL_$BOOT_VOLX;

/*
 * CAL_$DAYS_PER_MONTH - the 12 words at 0x00E817AC.
 * `gsk read 0xE817AC 24`: 001f 001d 001f 001e 001f 001e 001f 001f 001e 001f 001e 001f
 */
short CAL_$DAYS_PER_MONTH[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 * The MSM5832 RTC registers are memory-mapped at fixed addresses and are
 * reached through the CAL_$RTC_* macros in cal/cal.h, so there is nothing
 * to define here.
 */
