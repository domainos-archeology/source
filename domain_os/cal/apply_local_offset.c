/*
 * CAL_$APPLY_LOCAL_OFFSET - UTC clock -> local clock
 *
 * Adds the timezone's UTC delta (minutes, signed) to a 48-bit clock value.
 *
 * Parameters:
 *   clock - the clock_t to adjust in place ((0x8,A6))
 *
 * Original address: 0x00e685f8, 60 bytes
 *
 *   00e685fc  pea (-0x8,A6)                    ; &off_clock (2nd SEC_TO_CLOCK arg)
 *   00e68600  move.w (0x00e7b030).l,D0w        ; CAL_$TIMEZONE.utc_delta
 *   00e68606  ext.l D0                         ; SIGNED
 *   00e68608  lsl.l #0x2,D0 / move.l D0,D1 / neg.l D0 / lsl.l #0x4,D1 / add.l D1,D0
 *                                              ; delta*64 - delta*4 = delta*60
 *   00e68612  move.l D0,(-0xc,A6) / pea (-0xc,A6)
 *   00e6861a  jsr CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock)
 *   00e68622  pea (-0x8,A6) / move.l (0x8,A6),-(SP)
 *   00e6862a  jsr ADD48(clock, &off_clock)     ; args reclaimed by unlk
 */

#include "cal/cal_internal.h"

void CAL_$APPLY_LOCAL_OFFSET(clock_t *clock)
{
    uint off_seconds;       /* A6-0xC */
    clock_t off_clock;      /* A6-0x8 */

    /* 0x00E68600..0x00E68612: sign-extended minutes * 60 */
    off_seconds = (uint)((int32_t)CAL_$TIMEZONE.utc_delta * 60);

    /* 0x00E68616..0x00E68620 */
    CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock);

    /* 0x00E68622..0x00E6862A */
    ADD48(clock, &off_clock);
}
