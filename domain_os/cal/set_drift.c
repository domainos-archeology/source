/*
 * CAL_$SET_DRIFT - Store the drift correction in the timezone record
 *
 * Parameters:
 *   drift - the clock_t to store ((0x8,A6))
 *
 * Original address: 0x00e68580, 26 bytes
 *
 *   00e68588  move.l (A0),(0x00e7b036).l      ; CAL_$TIMEZONE.drift.high
 *   00e6858e  move.w (0x4,A0),(0x00e7b03a).l  ; CAL_$TIMEZONE.drift.low
 */

#include "cal/cal_internal.h"

void CAL_$SET_DRIFT(clock_t *drift)
{
    /* 0x00E68588..0x00E6858E */
    CAL_$TIMEZONE.drift.high = drift->high;
    CAL_$TIMEZONE.drift.low = drift->low;
}
