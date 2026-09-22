/*
 * TIME_$SET_TIME_OF_DAY - Set the time of day
 *
 * Converts a Unix {seconds, microseconds} pair into the 48-bit clock, then
 * under a raised IPL re-bases TIME_$BOOT_TIME, the raw clock
 * (TIME_$CURRENT_CLOCKH/L) and the time-of-day cells so that the ticks the
 * hardware timer has accumulated since the last interrupt are accounted
 * for, and finally rewrites the calendar chip.  A time before the Apollo
 * epoch (1980) just zeroes the raw clock and stores the pair as given.
 *
 * Parameters (frame 0x00E16794..0x00E16798):
 *   0x08 tv     - tv[0] seconds since 1970, tv[1] microseconds (A2)
 *   0x0C status - status return; cleared and never set again
 *
 * Original address: 0x00e1678c, 338 bytes
 *
 * Frame: -0x30 apollo_secs, -0x2C weekday, -0x20 new_clock, -0x18 a 6-byte
 * slot used first for usec_ticks then for the ABS_CLOCK sample, -0x10
 * decoded[6].
 *
 *   00e1679e  moveq #-4,D2 / and.l (0x4,A2),D2         ; usecs & ~3
 *   00e167a4  cmpi.l #0x12cea600,(A2) / blt.w -> pre-epoch path (SIGNED)
 *   00e167ae  CAL_$SEC_TO_CLOCK(&(tv[0] - 0x12cea600), &new_clock)
 *   00e167ca  clr.w (-0x18) ; D0 = tv[1] bpl/addq #3/asr #2 ; move.l D0,(-0x16)
 *   00e167dc  ADD48(&new_clock, &usec_ticks)
 *   00e167ec  ori #0x700,SR
 *   00e167f0  tst.l CURRENT_CLOCKH / beq; BOOT_TIME += new.high - CURRENT_CLOCKH
 *   00e16808  TIME_$ABS_CLOCK(&(-0x18))
 *   00e16814  D1w = abs.low - TIME_$CLOCKL                (WORD subtract)
 *   00e1681e  CURRENT_CLOCKH = new.high; CURRENT_TIME = tv[0]
 *   00e1682c  D0 = D1w zero-extended; D3 = new.low zero-extended
 *   00e1683a  D4 = D3 - D0; bpl / subq.l #1,CURRENT_CLOCKH; CURRENT_CLOCKL = D4w
 *   00e16850  D2 -= D0 << 2; bpl / D2 += 1000000, CURRENT_TIME -= 1
 *   00e16864  CURRENT_USEC = D2
 *   00e1686a  andi #0xf8ff,SR                          ; forced IPL 0
 *   00e1686e  CAL_$DECODE_TIME / CAL_$WEEKDAY / CAL_$WRITE_CALENDAR
 *   00e168bc  pre-epoch: clr CURRENT_CLOCKH/L; CURRENT_TIME = tv[0]; CURRENT_USEC = D2
 */

#include "time/time_internal.h"

void TIME_$SET_TIME_OF_DAY(uint32_t *tv, status_$t *status)
{
    uint32_t usecs;             /* D2 */
    uint apollo_secs;           /* A6-0x30 */
    int32_t usec_ticks_count;   /* D0 at 0x00E167CE */
    clock_t new_clock;          /* A6-0x20 */
    clock_t usec_ticks;         /* A6-0x18 */
    clock_t abs_clock;          /* A6-0x18, reused */
    uint16_t elapsed;           /* D1w at 0x00E16814, then D0 zero-extended */
    int32_t new_low;            /* D4 */
    int16_t decoded[6];         /* A6-0x10 */
    int16_t weekday;            /* A6-0x2C */

    /* 0x00E1679C */
    *status = status_$ok;

    /* 0x00E1679E..0x00E167A0 */
    usecs = tv[1] & ~3u;

    /* 0x00E167A4: SIGNED compare against the 1980 epoch */
    if ((int32_t)tv[0] < (int32_t)APOLLO_EPOCH_OFFSET) {
        /* 0x00E168BC..0x00E168CE */
        TIME_$CURRENT_CLOCKH = 0;
        TIME_$CURRENT_CLOCKL = 0;
        TIME_$CURRENT_TIME = tv[0];
        TIME_$CURRENT_USEC = usecs;
        return;
    }

    /* 0x00E167AE..0x00E167C8 */
    apollo_secs = tv[0] - APOLLO_EPOCH_OFFSET;
    CAL_$SEC_TO_CLOCK(&apollo_secs, &new_clock);

    /*
     * 0x00E167CA..0x00E167EA: the RAW microseconds (not the masked D2) are
     * divided by four toward zero and stored as a longword at -0x16,
     * straddling the record's high/low split.
     */
    usec_ticks_count = (int32_t)tv[1];
    if (usec_ticks_count < 0) {
        usec_ticks_count += 3;
    }
    usec_ticks_count >>= 2;
    usec_ticks.high = (uint32_t)usec_ticks_count >> 16;
    usec_ticks.low = (uint16_t)((uint32_t)usec_ticks_count & 0xFFFFu);
    ADD48(&new_clock, &usec_ticks);

    /* 0x00E167EC: ori #0x700,SR - raised, nothing saved */
    SET_IPL7();

    /* 0x00E167F0..0x00E16802 */
    if (TIME_$CURRENT_CLOCKH != 0) {
        TIME_$BOOT_TIME += new_clock.high - TIME_$CURRENT_CLOCKH;
    }

    /* 0x00E16808..0x00E16812 */
    TIME_$ABS_CLOCK(&abs_clock);

    /* 0x00E16814..0x00E16818: a 16-bit subtract, later zero-extended */
    elapsed = (uint16_t)(abs_clock.low - TIME_$CLOCKL);

    /* 0x00E1681E..0x00E16826 */
    TIME_$CURRENT_CLOCKH = new_clock.high;
    TIME_$CURRENT_TIME = tv[0];

    /* 0x00E1682C..0x00E1684A: 32-bit new.low - elapsed, borrow into the high */
    new_low = (int32_t)new_clock.low - (int32_t)elapsed;
    if (new_low < 0) {
        TIME_$CURRENT_CLOCKH -= 1;
    }
    TIME_$CURRENT_CLOCKL = (uint16_t)new_low;

    /* 0x00E16850..0x00E16864: 4 us per elapsed tick */
    usecs -= (uint32_t)elapsed << 2;
    if ((int32_t)usecs < 0) {
        usecs += 1000000u;
        TIME_$CURRENT_TIME -= 1;
    }
    TIME_$CURRENT_USEC = usecs;

    /* 0x00E1686A: andi #0xf8ff,SR - forces IPL 0 */
    SET_IPL0();

    /* 0x00E1686E..0x00E1687C */
    CAL_$DECODE_TIME(&new_clock, decoded);

    /* 0x00E1687E..0x00E16894 */
    weekday = CAL_$WEEKDAY(&decoded[0], &decoded[1], &decoded[2]);

    /* 0x00E16898..0x00E168B4: args reclaimed by unlk */
    CAL_$WRITE_CALENDAR(&decoded[0], &decoded[1], &decoded[2], &weekday,
                        &decoded[3], &decoded[4], &decoded[5]);
}
