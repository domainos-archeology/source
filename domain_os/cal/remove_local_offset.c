/*
 * CAL_$REMOVE_LOCAL_OFFSET - local clock -> UTC clock
 *
 * Subtracts the timezone's UTC delta (minutes, signed) from a 48-bit clock
 * value.  The mirror of CAL_$APPLY_LOCAL_OFFSET (0x00E685F8), with SUB48
 * in place of ADD48; SUB48's sign result in D0 is not looked at.
 *
 * Parameters:
 *   clock - the clock_t to adjust in place ((0x8,A6))
 *
 * Original address: 0x00e68634, 60 bytes
 *
 *   00e6863c  move.w (0x00e7b030).l,D0w / ext.l D0     ; utc_delta, SIGNED
 *   00e68644..00e6864c  D0 = delta*64 - delta*4 = delta*60
 *   00e6864e  move.l D0,(-0xc,A6) / pea (-0xc,A6)
 *   00e68656  jsr CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock)
 *   00e6865e  pea (-0x8,A6) / move.l (0x8,A6),-(SP)
 *   00e68666  jsr SUB48(clock, &off_clock)             ; args reclaimed by unlk
 */

#include "cal/cal_internal.h"

void CAL_$REMOVE_LOCAL_OFFSET(clock_t *clock)
{
    uint off_seconds;       /* A6-0xC */
    clock_t off_clock;      /* A6-0x8 */

    /* 0x00E6863C..0x00E6864E */
    off_seconds = (uint)((int32_t)CAL_$TIMEZONE.utc_delta * 60);

    /* 0x00E68652..0x00E6865C */
    CAL_$SEC_TO_CLOCK(&off_seconds, &off_clock);

    /* 0x00E6865E..0x00E68666: the D0 result is ignored */
    (void)SUB48(clock, &off_clock);
}
