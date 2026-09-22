/*
 * CAL_$GET_LOCAL_TIME - Current local time as a 48-bit clock
 *
 * Reads the RAW clock (TIME_$CLOCK, 0x00E2AFD6 - not TIME_$ABS_CLOCK), then
 * adds the timezone offset and the timezone record's drift correction.
 *
 * Parameters:
 *   clock - receives the local clock ((0x8,A6), A2)
 *
 * Original address: 0x00e6859a, 94 bytes
 *
 *   00e685a4  pea (-0x8,A6)                    ; &off_clock
 *   00e685a8  move.w (0x00e7b030).l,D0w / ext.l D0     ; utc_delta, SIGNED
 *   00e685b0..00e685b8  D0 = delta*64 - delta*4 = delta*60
 *   00e685ba  move.l D0,(-0xc,A6) / pea (-0xc,A6)
 *   00e685c2  jsr CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock)
 *   00e685ca  pea (A2) / jsr TIME_$CLOCK(clock)
 *   00e685d4  pea (-0x8,A6) / pea (A2) / jsr ADD48(clock, &off_clock)
 *   00e685e2  pea (0xe7b036).l / pea (A2) / jsr ADD48(clock, &CAL_$TIMEZONE.drift)
 *             ; 0xE7B036 = 0xE7B030 + 6; the last call's args are reclaimed by unlk
 */

#include "cal/cal_internal.h"

void CAL_$GET_LOCAL_TIME(clock_t *clock)
{
    uint off_seconds;       /* A6-0xC */
    clock_t off_clock;      /* A6-0x8 */

    /* 0x00E685A8..0x00E685BA */
    off_seconds = (uint)((int32_t)CAL_$TIMEZONE.utc_delta * 60);

    /* 0x00E685BE..0x00E685C8 */
    CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock);

    /* 0x00E685CA..0x00E685D2 */
    TIME_$CLOCK(clock);

    /* 0x00E685D4..0x00E685E0 */
    ADD48(clock, &off_clock);

    /* 0x00E685E2..0x00E685EA */
    ADD48(clock, &CAL_$TIMEZONE.drift);
}
