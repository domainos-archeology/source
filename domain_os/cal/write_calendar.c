/*
 * CAL_$WRITE_CALENDAR - Set the OKI MSM5832 real-time clock
 *
 * Writes the 13 BCD digit registers of the MSM5832 in descending address
 * order (Y10 = address 12 down to S1 = address 0).  The control byte in D0
 * starts at 0xC1 (address 12 with the HOLD pin asserted) and is decremented
 * by 0x10 -- one register address -- by the digit writer itself, so it must
 * be threaded by reference when the Pascal nested procedures are flattened.
 *
 * Parameters (all Pascal `var`, pushed right to left, caller cleans up):
 *   (0x1C,SP) year    - full year, e.g. 1985 (only year mod 100 is stored)
 *   (0x20,SP) month   - 1..12
 *   (0x24,SP) day     - 1..31
 *   (0x28,SP) weekday - 0..6 (single digit register W)
 *   (0x2C,SP) hour    - 0..23
 *   (0x30,SP) minute  - 0..59
 *   (0x34,SP) second  - 0..59
 * (0x18,SP) is the return address; the displacements above are what the
 * assembly uses after `movem.l {D7 D6 D5 D4 D3 D2},-(SP)` at 0x00E8167C.
 *
 * Original address: 0x00E8167C
 * Size: 136 bytes
 * Nested procedures (Pascal, flattened to statics below):
 *   cal_$write_calendar_0_to_99  0x00E81704
 *   cal_$write_calendar_digit    0x00E81716
 *   cal_$finish_write            0x00E81744
 *   cal_$delay_40                0x00E81752  (falls through into cal_$delay)
 *   cal_$delay                   0x00E81756
 */

#include "cal/cal_internal.h"
#include "network/network.h"

/* Control byte for the first register written: Y10 (address 12) + HOLD */
#define CAL_WRITE_START_CONTROL CAL_$RTC_CTL_ADDR_HOLD(MSM5832_REG_Y10) /* 0xC1 */

/* Delay counts baked into the code */
#define CAL_WRITE_INITIAL_DELAY 0xC8 /* 0x00E81696: move.w #0xC8,D5w */
#define CAL_WRITE_STROBE_DELAY  0x28 /* 0x00E81752: move.w #0x28,D5w */

/*
 * "Add 0x50 to the value before splitting it into tens and ones" sets bit 3
 * of the tens digit, because (v + 80) / 10 == v / 10 + 8 and (v + 80) % 10 ==
 * v % 10.  The original uses that idiom twice: correctly on the hour (H10 bit
 * 3 is the MSM5832's 24-hour select) and incorrectly on the date (D10 bit 3 is
 * unused; the leap-year flag is D10 bit 2).  See the resolved note in
 * cal/cal.h and in CAL_$WRITE_CALENDAR below.
 */
#define CAL_WRITE_TENS_BIT3 0x50

/* Internal helper functions (Pascal nested procedures, static here) */
static void cal_$delay(int16_t count);
static void cal_$delay_40(void);
static void cal_$write_calendar_digit(int16_t *control, int16_t digit);
static void cal_$write_calendar_0_to_99(int16_t *control, int16_t value);
static void cal_$finish_write(void);

/*
 * cal_$delay - 0x00E81756
 *
 *   subq.w #0x1,D5w
 *   bge.b  cal_$delay
 *   rts
 *
 * The counter is decremented until it goes negative, so the loop body runs
 * count + 1 times for a non-negative count.  ARCH_SPIN_TICK() keeps the loop
 * from being deleted; the original is pure CPU time used as a device delay.
 */
static void cal_$delay(int16_t count)
{
    do {
        ARCH_SPIN_TICK();
        count = (int16_t)(count - 1);
    } while (count >= 0);
}

/*
 * cal_$delay_40 - 0x00E81752
 *
 *   move.w #0x28,D5w      (and then falls through into cal_$delay)
 */
static void cal_$delay_40(void)
{
    cal_$delay(CAL_WRITE_STROBE_DELAY);
}

/*
 * cal_$write_calendar_digit - 0x00E81716
 *
 * Writes one 4-bit digit to the register currently selected by *control and
 * then steps *control down to the next MSM5832 register.
 *
 *   not.b  D1b                     ; the interface inverts the data lines
 *   move.b D1b,(0x00FFA822).l      ; write-data port
 *   move.b D0b,(0x00FFA820).l      ; address + HOLD
 *   bsr    cal_$delay_40
 *   move.b D0b,D5b / or.b #2,D5b
 *   move.b D5b,(0x00FFA820).l      ; assert WRITE
 *   bsr    cal_$delay_40
 *   move.b D0b,(0x00FFA820).l      ; deassert WRITE
 *   bsr    cal_$delay_40
 *   sub.w  #0x10,D0w               ; <- mutates the caller's control byte
 */
static void cal_$write_calendar_digit(int16_t *control, int16_t digit)
{
    CAL_$RTC_WRITE_DATA((uint8_t)~(uint8_t)digit);
    CAL_$RTC_WRITE_CONTROL((uint8_t)*control);
    cal_$delay_40();

    CAL_$RTC_WRITE_CONTROL((uint8_t)((uint8_t)*control | CAL_$RTC_CTL_WRITE));
    cal_$delay_40();

    CAL_$RTC_WRITE_CONTROL((uint8_t)*control);
    cal_$delay_40();

    /* 0x00E8173E */
    *control = (int16_t)(*control - CAL_$RTC_CTL_ADDR_STEP);
}

/*
 * cal_$write_calendar_0_to_99 - 0x00E81704
 *
 *   ext.l  D1
 *   divu.w #0xa,D1        ; quotient (tens) low word, remainder (ones) high
 *   move.l D1,D4
 *   bsr    cal_$write_calendar_digit    ; tens
 *   swap   D4 / move.w D4w,D1w
 *   bsr    cal_$write_calendar_digit    ; ones
 *
 * divu.w is an UNSIGNED divide of the sign-extended word, so a negative
 * argument would overflow and trap on the 68000; every caller passes a
 * non-negative value.
 */
static void cal_$write_calendar_0_to_99(int16_t *control, int16_t value)
{
    uint32_t extended = (uint32_t)(int32_t)value;
    uint16_t tens = (uint16_t)(extended / 10u);
    uint16_t ones = (uint16_t)(extended % 10u);

    cal_$write_calendar_digit(control, (int16_t)tens);
    cal_$write_calendar_digit(control, (int16_t)ones);
}

/*
 * cal_$finish_write - 0x00E81744
 *
 *   move.b #0x0,(0x00FFA820).l   ; release HOLD, deselect
 */
static void cal_$finish_write(void)
{
    CAL_$RTC_WRITE_CONTROL(0);
}

void CAL_$WRITE_CALENDAR(int16_t *year, int16_t *month, int16_t *day,
                         int16_t *weekday, int16_t *hour, int16_t *minute,
                         int16_t *second)
{
    int16_t control;      /* D0w */
    int16_t year_2digit;  /* D2w */
    int16_t month_val;    /* D3w */
    int16_t day_val;      /* D1w */

    /*
     * 0x00E81680: a diskless node has no calendar chip of its own; skip the
     * whole sequence (the branch target is the movem restore, so not even the
     * final control write happens).
     */
    if (NETWORK_$REALLY_DISKLESS != 0) {
        return;
    }

    /* 0x00E8168A: the register address is set up before the first port write */
    control = CAL_WRITE_START_CONTROL;

    /* 0x00E8168E: assert HOLD and let the counter chain settle */
    CAL_$RTC_WRITE_CONTROL(CAL_$RTC_CTL_HOLD);
    cal_$delay(CAL_WRITE_INITIAL_DELAY);

    /*
     * 0x00E8169E: ext.l / divu.w #0x64 / swap - the *remainder* (year mod
     * 100) is what gets written, and it is also kept in D2 for the leap test.
     */
    year_2digit = (int16_t)((uint32_t)(int32_t)*year % 100u);
    cal_$write_calendar_0_to_99(&control, year_2digit);   /* Y10, Y1 */

    month_val = *month;
    cal_$write_calendar_0_to_99(&control, month_val);     /* MO10, MO1 */

    day_val = *day;

    /*
     * 0x00E816C0: the MSM5832's leap-year bit describes the February that
     * ends the current March-based year, so for March..December the test uses
     * year + 1.  0x00E816C8 wraps 100 back to 0 (behaviourally a no-op for
     * the & 3 that follows, but preserved).
     */
    if (month_val > 2) {
        year_2digit = (int16_t)(year_2digit + 1);
        if (year_2digit >= 100) {
            year_2digit = 0;
        }
    }
    if ((year_2digit & 3) == 0) {
        /*
         * 0x00E816D6: add.w #0x50,D1w.
         *
         * ORIGINAL BUG, reproduced as found (bead source-tcxm, resolved).
         *
         * The MSM5832 D10 register is {D0,D1 = date tens, D2 = leap year,
         * D3 = unused}.  Adding 80 before the /10 split puts the flag in the
         * tens digit's bit 3 -- the unused bit -- so the chip's leap-year flag
         * is never programmed by this routine.  The correct constant would
         * have been 0x28 (add 40 == +4 in the tens digit, i.e. D10 bit 2),
         * which is what TIME_$READ_CAL uses on the read side (`bset.l #2` at
         * 0x00E2AE78, and `and.w #3` at 0x00E2AEC8 when decoding the date).
         * The identical `add.w #0x50` on the hour at 0x00E816EA below IS
         * correct, because H10 bit 3 is the 24-hour select; the idiom was
         * evidently copied onto the wrong register.
         *
         * The mistake is benign for date read-back (the chip ignores D10 D3
         * and the reader masks with 3) and TIME_$READ_CAL rewrites D10 with
         * bit 2 set on every read taken in a leap year.
         */
        day_val = (int16_t)(day_val + CAL_WRITE_TENS_BIT3);
    }
    cal_$write_calendar_0_to_99(&control, day_val);       /* D10, D1 */

    /* 0x00E816E2: the weekday is a single register (W, address 6) */
    cal_$write_calendar_digit(&control, *weekday);

    /*
     * 0x00E816EA: add.w #0x50 puts bit 3 into the H10 register, which is the
     * MSM5832's 24-hour select (MSM5832_H10_24H_FLAG).  Correct here, unlike
     * the same idiom on the date above.
     */
    cal_$write_calendar_0_to_99(&control,
                                (int16_t)(*hour + CAL_WRITE_TENS_BIT3)); /* H10, H1 */

    cal_$write_calendar_0_to_99(&control, *minute);       /* MI10, MI1 */
    cal_$write_calendar_0_to_99(&control, *second);       /* S10, S1 */

    /* 0x00E81700 -> 0x00E81744 */
    cal_$finish_write();
}
