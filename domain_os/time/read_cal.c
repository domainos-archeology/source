/*
 * TIME_$READ_CAL - Read calendar from hardware RTC
 *
 * Reads 13 BCD digits from the battery-backed real-time clock chip
 * (accessed via memory-mapped I/O at base address 0x00FFA800) and
 * converts them to:
 *   - A 48-bit clock value (4-microsecond ticks since Apollo epoch)
 *   - A 32-bit timestamp (seconds since Apollo epoch, Jan 1 1980)
 *
 * The RTC chip stores time as 13 BCD digits:
 *   year(2), month(2), day(2), weekday(1), hour(2), minute(2), second(2)
 *
 * The day_tens digit has a leap year flag in bit 2 (set by
 * CAL_$WRITE_CALENDAR with the +0x50 offset on day). Similarly,
 * the hour_tens digit has a 24-hour mode flag in bit 2.
 *
 * Uses a March-based calendar for the day count computation:
 *   March = month 0, February = month 11.
 * The epoch is normalized to approximately March 1, 1981 in the
 * March-based system (since the Apollo epoch is Jan 1, 1980).
 *
 * If the current year is a leap year in the March-based system,
 * the function writes back the day_tens digit with the leap year
 * flag (bit 2) set to the RTC chip.
 *
 * Called by TIME_$INIT during system boot to set the initial clock.
 *
 * Parameters:
 *   clock - Pointer to receive 48-bit clock value (ticks)
 *   time  - Pointer to receive 32-bit seconds since epoch
 *
 * Original address: 0x00e2af5e (gate entry), 0x00e2adfc (body)
 *
 * Nested procedures (Pascal-style, now static helpers):
 *   time_$read_cal_delay      at 0x00e2af58 (generic delay loop)
 *   time_$read_cal_delay_20   at 0x00e2af54 (delay of 20 iterations)
 */

#include "time/time_internal.h"

/*
 * RTC digit index constants.
 * Digits are read from the chip in order of decreasing control value
 * (0xC5 down to 0x05 in steps of 0x10), giving 13 digits.
 */
#define DIGIT_YEAR_TENS     0
#define DIGIT_YEAR_ONES     1
#define DIGIT_MONTH_TENS    2
#define DIGIT_MONTH_ONES    3
#define DIGIT_DAY_TENS      4   /* Bit 2 = leap year flag */
#define DIGIT_DAY_ONES      5
#define DIGIT_WEEKDAY       6
#define DIGIT_HOUR_TENS     7   /* Bit 2 = 24-hour mode flag */
#define DIGIT_HOUR_ONES     8
#define DIGIT_MINUTE_TENS   9
#define DIGIT_MINUTE_ONES   10
#define DIGIT_SECOND_TENS   11
#define DIGIT_SECOND_ONES   12
#define RTC_NUM_DIGITS      13

/* RTC control register values */
#define RTC_READ_START_CONTROL  0xC5    /* First digit read control value */
#define RTC_CONTROL_STEP        0x10    /* Decrement per digit */
#define RTC_INITIAL_DELAY       0xC8    /* Initial delay count (200) */

/* Day/hour digit flag masks */
#define RTC_DIGIT_VALUE_MASK    0x03    /* Mask off flags, keep tens digit (0-3) */
#define RTC_LEAP_YEAR_FLAG      0x04    /* Bit 2 in day_tens = leap year */

/*
 * Delay loop: counts down from 'count' to 0.
 * Corresponds to FUN_00e2af58 at 0x00e2af58.
 */
static void time_$read_cal_delay(short count)
{
    do {
        count -= 1;
    } while (count != 0);
}

/*
 * Standard delay of 0x14 (20) iterations for RTC digit reads.
 * Corresponds to FUN_00e2af54 at 0x00e2af54.
 * (Falls through to the delay loop in the original assembly.)
 */
static void time_$read_cal_delay_20(void)
{
    time_$read_cal_delay(0x14);
}

void TIME_$READ_CAL(clock_t *clock, uint32_t *time)
{
    short digits[RTC_NUM_DIGITS];
    short control;
    int i;
    short month;
    short march_month;     /* Month in March-based calendar (0=March, 11=February) */
    short march_flag;      /* 1 if original month >= 3, else 0 */
    short year_2digit;
    uint16_t year_from_epoch;
    uint16_t total_days;
    uint32_t total_seconds;
    uint32_t product_low;
    uint32_t product_high;
    uint16_t sec_low;
    uint16_t sec_high;

    /*
     * Start RTC read sequence.
     * Write 1 to control register to initiate.
     */
    CAL_$CONTROL_VIRTUAL_ADDR = 1;

    /* Initial delay (0xC8 = 200 iterations) before first read */
    control = RTC_READ_START_CONTROL;
    time_$read_cal_delay(RTC_INITIAL_DELAY);

    /*
     * Read 13 BCD digits from the RTC chip.
     *
     * For each digit:
     *   1. Write control value to select the digit register
     *   2. Delay for chip timing
     *   3. Read data byte, invert, and mask to 4 bits
     *
     * Control values: 0xC5, 0xB5, 0xA5, ..., 0x15, 0x05
     * (High nibble selects digit position, low nibble = read mode)
     *
     * Data is stored inverted in the chip (ones' complement),
     * matching the write function which stores ~digit.
     */
    i = 0;
    do {
        CAL_$CONTROL_VIRTUAL_ADDR = (char)control;
        time_$read_cal_delay_20();
        digits[i] = (~(short)(uint8_t)CAL_$READ_DATA_VIRTUAL_ADDR) & 0xF;
        control -= RTC_CONTROL_STEP;
        i++;
    } while (control >= 0);

    /* Reset control to 1 (prepare for potential write-back) */
    CAL_$CONTROL_VIRTUAL_ADDR = 1;

    /*
     * Reconstruct month value (1-12) from BCD digits.
     */
    month = digits[DIGIT_MONTH_TENS] * 10 + digits[DIGIT_MONTH_ONES];

    /*
     * Convert to March-based calendar.
     * In this system, March = month 0, February = month 11.
     * This simplifies leap year handling since the leap day
     * (Feb 29) falls at the end of the "year".
     */
    march_flag = 1;
    march_month = month - 3;
    if (march_month < 0) {
        march_flag = 0;
        march_month += 12;
    }

    /*
     * Reconstruct 2-digit year and adjust for March-based calendar.
     * If month >= March, the March-based year is year+1
     * (since we haven't finished the current March-based year yet).
     */
    year_2digit = digits[DIGIT_YEAR_TENS] * 10 + digits[DIGIT_YEAR_ONES];
    year_2digit += march_flag;

    /*
     * Normalize to years since March 1981.
     * (The Apollo epoch is Jan 1, 1980. In March-based terms,
     * the first complete year starts March 1980, which after
     * adding march_flag for month>=3 gives year 81.)
     *
     * 0x51 = 81 decimal.
     */
    year_from_epoch = (uint16_t)(year_2digit - 0x51);
    if ((int16_t)year_from_epoch < 0) {
        year_from_epoch += 100;
    }

    /*
     * Leap year check in March-based calendar.
     * (year_from_epoch + 1) divisible by 4 means this March-based
     * year contains a February 29.
     *
     * If leap year, write back the day_tens digit with bit 2 set
     * (the leap year flag) to the RTC chip.
     */
    if (((year_from_epoch + 1) & 3) == 0) {
        short day_tens_with_flag = digits[DIGIT_DAY_TENS] | RTC_LEAP_YEAR_FLAG;
        CAL_$WRITE_DATA_VIRTUAL_ADDR = (char)~day_tens_with_flag;
        CAL_$CONTROL_VIRTUAL_ADDR = (char)0x81;      /* Select day_tens for write */
        CAL_$CONTROL_VIRTUAL_ADDR = (char)0x83;      /* Strobe (bit 1 set) */
        time_$read_cal_delay_20();
        CAL_$CONTROL_VIRTUAL_ADDR = (char)0x81;      /* Clear strobe */
    }

    /* Finish RTC access - clear control register */
    CAL_$CONTROL_VIRTUAL_ADDR = 0;

    /*
     * ================================================================
     * Compute total days since epoch.
     *
     * Formula (March-based calendar):
     *   days = year_from_epoch * 365
     *        + year_from_epoch / 4        (leap days)
     *        + march_month * 30           (base days per month)
     *        + (march_month / 5) * 3      (correction for 31-day months)
     *        + ((march_month % 5) + 1) / 2  (fine correction)
     *        + day_of_month
     *        + 59                          (Jan + Feb = 31 + 28)
     *
     * The month formula approximates cumulative days from March:
     *   month 0 (Mar): 0     month 6 (Sep): 184
     *   month 1 (Apr): 31    month 7 (Oct): 214
     *   month 2 (May): 61    month 8 (Nov): 245
     *   month 3 (Jun): 92    month 9 (Dec): 275
     *   month 4 (Jul): 122   month 10 (Jan): 306
     *   month 5 (Aug): 153   month 11 (Feb): 337
     * ================================================================
     */
    total_days = (uint16_t)(year_from_epoch * 365) + (year_from_epoch >> 2);
    total_days += (uint16_t)((uint16_t)march_month * 30);
    total_days += (uint16_t)((uint16_t)(march_month / 5) * 3);
    total_days += (uint16_t)(((uint16_t)(march_month % 5) + 1) >> 1);

    /* Add day of month (mask off leap year flag in day_tens bit 2) */
    total_days += (uint16_t)((digits[DIGIT_DAY_TENS] & RTC_DIGIT_VALUE_MASK) * 10
                              + digits[DIGIT_DAY_ONES]);

    /* Add 59 for January (31) + February (28) to convert from March-based */
    total_days += 59;

    /*
     * ================================================================
     * Compute total seconds = day_seconds + time_of_day_seconds.
     *
     * Hours: mask off 24-hour flag in hour_tens bit 2.
     * ================================================================
     */

    /* Hours * 60 + minutes */
    total_seconds = (uint32_t)(uint16_t)(
        ((digits[DIGIT_HOUR_TENS] & RTC_DIGIT_VALUE_MASK) * 10
         + digits[DIGIT_HOUR_ONES]) * 60);

    /* Add minutes */
    total_seconds += (uint16_t)(digits[DIGIT_MINUTE_TENS] * 10
                                + digits[DIGIT_MINUTE_ONES]);

    /* Convert (hours*60 + minutes) to seconds */
    total_seconds = (uint32_t)(uint16_t)total_seconds * 60;

    /* Add seconds (32-bit add to handle full day range) */
    total_seconds += (uint32_t)(uint16_t)(digits[DIGIT_SECOND_TENS] * 10)
                     + (uint16_t)digits[DIGIT_SECOND_ONES];

    /*
     * Convert total_days to seconds and add.
     * total_days * 86400 = total_days * 2 * 43200.
     *
     * Note: The multiplication uses 16-bit intermediate (total_days * 2)
     * which can overflow for years past ~2069. This matches the original
     * m68k code which uses asl.w (16-bit shift) + mulu.w.
     */
    total_seconds += (uint32_t)(uint16_t)(total_days * 2) * 43200u;

    /* Store the seconds-since-epoch timestamp */
    *time = total_seconds;

    /*
     * ================================================================
     * Convert seconds to 48-bit clock ticks.
     *
     * ticks = seconds * 250,000 = seconds * 0x3D090
     * 0x3D090 = 3 * 0x10000 + 0xD090
     *
     * Uses partial products since the m68k 68010 only has
     * 16x16->32 multiply (mulu.w):
     *
     *   Let SL = seconds & 0xFFFF, SH = seconds >> 16
     *   clock->low  = (SL * 0xD090) & 0xFFFF
     *   clock->high = (SL * 0xD090) >> 16
     *               + SL * 3
     *               + SH * 0xD090
     *   Then add SH * 3 to the upper 16 bits of clock->high.
     * ================================================================
     */
    sec_low = (uint16_t)(total_seconds & 0xFFFF);
    sec_high = (uint16_t)(total_seconds >> 16);

    /* Low 16 bits of ticks */
    product_low = (uint32_t)sec_low * 0xD090u;
    clock->low = (uint16_t)product_low;

    /* Middle/high 32 bits of ticks */
    product_high = (product_low >> 16)
                 + (uint32_t)sec_low * 3u
                 + (uint32_t)sec_high * 0xD090u;
    clock->high = product_high;

    /* Add sec_high * 3 to upper 16 bits of clock->high (portably) */
    clock->high = ((uint32_t)((uint16_t)(clock->high >> 16) + sec_high * 3) << 16)
                | (clock->high & 0xFFFF);
}
