/*
 * time_$itimer_to_clock - 48-bit right shift by one, itimer form -> clock form
 *
 * Halves the 48-bit value at *src_itimer into *dst_clock.  Bit 0 of the high
 * longword becomes bit 15 of the low word.
 *
 * Original address: 0x00e58c3a
 *
 * Assembly:
 *   00e58c3a  link.w A6,-0xc
 *   00e58c3e  movea.l (0xc,A6),A0        ; A0 = SOURCE (second argument)
 *   00e58c42  move.l (A0),D0
 *   00e58c44  lsr.l #0x1,D0              ; logical, not arithmetic
 *   00e58c46  move.l D0,(-0xc,A6)
 *   00e58c4a  move.w (0x4,A0),D1w
 *   00e58c4e  lsr.w #0x1,D1w
 *   00e58c50  move.w D1w,(-0x8,A6)
 *   00e58c54  btst.b #0x0,(0x3,A0)       ; bit 0 of the high longword
 *   00e58c5a  beq.b 0x00e58c62
 *   00e58c5c  bset.b #0x7,(-0x8,A6)      ; high byte of the word => bit 15
 *   00e58c62  movea.l (0x8,A6),A1        ; A1 = DESTINATION (first argument)
 *   00e58c66  move.l (-0xc,A6),(A1)
 *   00e58c6a  move.w (-0x8,A6),(0x4,A1)
 *
 * As in the sibling routine the result is staged in a temporary, so source
 * and destination may alias.
 */

#include "time/time_internal.h"

void time_$itimer_to_clock(clock_t *dst_clock, const clock_t *src_itimer)
{
    uint32_t tmp_high;
    uint16_t tmp_low;

    tmp_high = src_itimer->high >> 1;         /* 0xE58C44: lsr.l #1 */
    tmp_low = (uint16_t)(src_itimer->low >> 1); /* 0xE58C4E: lsr.w #1 */

    /* 0xE58C54: btst.b #0,(0x3,A0) is bit 0 of the big-endian longword */
    if ((src_itimer->high & 1) != 0) {
        tmp_low |= 0x8000;                     /* 0xE58C5C: bset.b #7 on the
                                                * high byte of the word */
    }

    dst_clock->high = tmp_high;
    dst_clock->low = tmp_low;
}
