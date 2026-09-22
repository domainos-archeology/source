/*
 * CAL_$GET_INFO - Copy the timezone record to the caller
 *
 * Parameters:
 *   info - receives CAL_$TIMEZONE ((0x8,A6))
 *
 * Original address: 0x00e68568, 24 bytes
 *
 *   00e6856c  movea.l #0xe7b030,A0     ; CAL_$TIMEZONE
 *   00e68572  movea.l (0x8,A6),A1
 *   00e68576  move.l (A0)+,(A1)+       ; three longwords = 12 bytes:
 *   00e68578  move.l (A0)+,(A1)+       ;   utc_delta, tz_name, drift
 *   00e6857a  move.l (A0)+,(A1)+
 *
 * Only the 12-byte record is copied; CAL_$LAST_VALID_TIME at +0x0C is not.
 */

#include "cal/cal_internal.h"

void CAL_$GET_INFO(cal_$timezone_rec_t *info)
{
    /* 0x00E68576..0x00E6857A */
    *info = CAL_$TIMEZONE;
}
