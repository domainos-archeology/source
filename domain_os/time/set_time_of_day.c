/*
 * TIME_$SET_TIME_OF_DAY - Set current time of day
 *
 * Sets the system time from a Unix timeval structure.
 * This affects all clock values and updates the hardware RTC.
 *
 * Parameters:
 *   tv - Pointer to timeval (tv[0]=seconds, tv[1]=microseconds)
 *   status - Status return
 *
 * Original address: 0x00e1678c
 *
 * The function:
 * 1. If time < Apollo epoch (0x12CEA600), just sets current time directly
 * 2. Otherwise, converts to clock ticks, adjusts boot time, and updates RTC
 *
 * Assembly:
 *   00e16794  movea.l (0x8,A6),A2 / movea.l (0xc,A6),A0 / clr.l (A0)
 *   00e1679e  moveq #-0x4,D2 / and.l (0x4,A2),D2   ; usecs rounded down to 4
 *   00e167a4  cmpi.l #0x12cea600,(A2) / blt        ; SIGNED
 *   00e167b4  sub 0x12cea600 / jsr CAL_$SEC_TO_CLOCK(&secs, &new_clock)
 *   00e167ca  clr.w (-0x18,A6) / move.l D0,(-0x16,A6)
 *                                       ; the 32-bit tick count is written
 *                                       ; STRADDLING the record's high/low
 *                                       ; split, so all 32 bits survive
 *   00e167ce  move.l (0x4,A2),D0 / bpl / addq.l #0x3 / asr.l #0x2
 *                                       ; a SIGNED divide by 4
 *   00e167e4  jsr ADD48(&new_clock, &usec_ticks)
 *   00e167ec  ori #0x700,SR             ; raise to IPL 7 without saving
 *   00e167f0..00e16802  boot time follows the clock
 *   00e1680c  jsr TIME_$ABS_CLOCK(-0x18) ; REUSES the usec_ticks slot
 *   00e16814..00e16864  the clock and time-of-day updates
 *   00e1686a  andi #-0x701,SR           ; force IPL 0; NOT a restore
 *   00e16876  jsr CAL_$DECODE_TIME(&new_clock, decoded)
 *   00e1688a  jsr CAL_$WEEKDAY(&year, &month, &day)
 *   00e168b4  jsr CAL_$WRITE_CALENDAR(year, month, day, weekday,
 *                                     hour, minute, second)
 *   00e168bc  the pre-epoch path
 */

#include "time/time_internal.h"
#include "cal/cal.h"

/* Apollo epoch offset (seconds from 1970 to 1980) */
#define APOLLO_EPOCH_OFFSET 0x12CEA600

void TIME_$SET_TIME_OF_DAY(uint32_t *tv, status_$t *status)
{
    uint32_t seconds;
    uint32_t usecs;
    uint32_t unix_secs;
    uint32_t usec_ticks_count;
    clock_t new_clock;          /* A6-0x20 */
    clock_t current_abs;        /* A6-0x18, the usec_ticks slot reused */
    clock_t usec_ticks;         /* A6-0x18 */
    int16_t decoded[6];         /* A6-0x10: year, month, day, hour, min, sec */
    int16_t weekday;            /* A6-0x2C */

    *status = status_$ok;

    seconds = tv[0];
    usecs = tv[1] & ~0x3u;  /* 0xE1679E: moveq #-0x4,D2 / and.l */

    /* If time is before Apollo epoch, handle specially */
    if ((int32_t)seconds < (int32_t)APOLLO_EPOCH_OFFSET) {
        TIME_$CURRENT_CLOCKH = 0;
        TIME_$CURRENT_CLOCKL = 0;
        TIME_$CURRENT_TIME = seconds;
        TIME_$CURRENT_USEC = usecs;
        return;
    }

    /* Convert Unix time to Apollo time (seconds since 1980) */
    unix_secs = seconds - APOLLO_EPOCH_OFFSET;

    /* Convert seconds to 48-bit clock ticks */
    CAL_$SEC_TO_CLOCK(&unix_secs, &new_clock);

    /*
     * 0xE167CE: the tick count is the RAW microseconds divided by four with
     * SIGNED rounding toward zero, and 0xE167CA/0xE167D8 place all 32 bits of
     * it in the 48-bit record - the word at +0 is cleared and the longword is
     * stored at +2, straddling the high/low boundary.  A count above 0xFFFF
     * (any usec value over 0x3FFFC) therefore reaches `high`.
     */
    usec_ticks_count = (uint32_t)((int32_t)tv[1] / 4);
    usec_ticks.high = usec_ticks_count >> 16;
    usec_ticks.low = (uint16_t)(usec_ticks_count & 0xFFFFu);
    ADD48(&new_clock, &usec_ticks);

    /* 0xE167EC: a bare "ori #0x700,SR" - nothing is saved */
    SET_IPL7();

    /* Adjust boot time if we have a valid current clock */
    if (TIME_$CURRENT_CLOCKH != 0) {
        TIME_$BOOT_TIME = (new_clock.high - TIME_$CURRENT_CLOCKH) + TIME_$BOOT_TIME;
    }

    /* Get current absolute clock */
    TIME_$ABS_CLOCK(&current_abs);

    /* Update current clock values */
    TIME_$CURRENT_CLOCKH = new_clock.high;
    TIME_$CURRENT_TIME = seconds;

    /* Calculate new CLOCKL accounting for elapsed time */
    int32_t diff = (int32_t)new_clock.low - (int32_t)(current_abs.low - TIME_$CLOCKL);
    if (diff < 0) {
        TIME_$CURRENT_CLOCKH--;
    }
    TIME_$CURRENT_CLOCKL = (uint16_t)diff;

    /* Update microseconds accounting for elapsed time */
    TIME_$CURRENT_USEC = usecs - (current_abs.low - TIME_$CLOCKL) * 4;
    if ((int32_t)TIME_$CURRENT_USEC < 0) {
        TIME_$CURRENT_USEC += 1000000;
        TIME_$CURRENT_TIME--;
    }

    /* 0xE1686A: "andi #-0x701,SR" forces IPL 0; it does not restore */
    SET_IPL0();

    /* 0xE16876 */
    CAL_$DECODE_TIME(&new_clock, decoded);

    /* 0xE1688A: the weekday goes in its own slot, not into `decoded` */
    weekday = CAL_$WEEKDAY(&decoded[0], &decoded[1], &decoded[2]);

    /* 0xE168B4 */
    CAL_$WRITE_CALENDAR(&decoded[0], &decoded[1], &decoded[2], &weekday,
                        &decoded[3], &decoded[4], &decoded[5]);
}
