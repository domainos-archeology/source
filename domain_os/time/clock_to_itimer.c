/*
 * time_$clock_to_itimer - 48-bit left shift by one, clock form -> itimer form
 *
 * Doubles the 48-bit value at *src_clock into *dst_itimer.  The record is
 * {high:32 at +0, low:16 at +4}, so the carry runs from the low word into the
 * high longword.
 *
 * Original address: 0x00e58c02
 *
 * Assembly:
 *   00e58c02  link.w A6,-0xc
 *   00e58c06  movea.l (0xc,A6),A0        ; A0 = SOURCE (second argument)
 *   00e58c0a  move.l (A0),D0
 *   00e58c0c  add.l D0,D0
 *   00e58c0e  move.l D0,(-0xc,A6)        ; tmp.high = src->high * 2
 *   00e58c12  move.w (0x4,A0),D1w
 *   00e58c16  add.w D1w,D1w
 *   00e58c18  move.w D1w,(-0x8,A6)       ; tmp.low  = src->low * 2
 *   00e58c1c  cmpi.w #-0x8000,(0x4,A0)
 *   00e58c22  bcs.b 0x00e58c28           ; unsigned: src->low < 0x8000
 *   00e58c24  addq.l #0x1,(-0xc,A6)      ; carry bit 15 of low into high
 *   00e58c28  movea.l (0x8,A6),A1        ; A1 = DESTINATION (first argument)
 *   00e58c2c  move.l (-0xc,A6),(A1)
 *   00e58c30  move.w (-0x8,A6),(0x4,A1)
 *
 * The temporary matters: a caller may pass the same record as source and
 * destination, and the whole shift is computed before either store.
 */

#include "time/time_internal.h"

void time_$clock_to_itimer(clock_t *dst_itimer, const clock_t *src_clock)
{
    uint32_t tmp_high;
    uint16_t tmp_low;

    tmp_high = src_clock->high * 2;          /* 0xE58C0C: add.l D0,D0 */
    tmp_low = (uint16_t)(src_clock->low * 2); /* 0xE58C16: add.w D1w,D1w */

    /* 0xE58C1C/0xE58C22: bcs is unsigned, so this is "low >= 0x8000" */
    if (src_clock->low >= 0x8000) {
        tmp_high = tmp_high + 1;
    }

    dst_itimer->high = tmp_high;
    dst_itimer->low = tmp_low;
}
