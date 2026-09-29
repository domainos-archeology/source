/*
 * time/test/test_read_cal.c - Unit tests for TIME_$READ_CAL (0x00E2ADFC)
 *
 * The real time/read_cal.c is #included below and driven through a mock
 * OKI MSM5832: the host implementations of cal_$rtc_write_reg /
 * cal_$rtc_read_reg (declared in cal/cal.h for ARCH_HOST) decode the control
 * byte exactly as the chip would, so the function walks the real read
 * protocol - assert HOLD, select each register with READ+HOLD, complement the
 * inverted data - and the tests assert on what it actually returns.
 *
 * Cross-checks use an independent Gregorian seconds-since-1980 computation
 * and a 64-bit multiply, not a copy of the assembly's formulas.
 */

#include "time/time_internal.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Mock OKI MSM5832
 *
 * regs[] is indexed by the MSM5832 register address (0 = S1 .. 12 = Y10) and
 * holds the true 4-bit digit; the Apollo interface inverts the data lines, so
 * the read port hands back the complement.
 * ============================================================================ */

static uint8_t rtc_regs[MSM5832_NUM_REGS];
static int rtc_selected;          /* register latched by the last READ+HOLD */
static uint8_t rtc_pending_data;  /* last value put on the write-data port */

/* Log of every control byte written, in order */
#define MAX_CTRL 64
static uint8_t rtc_control_log[MAX_CTRL];
static int rtc_control_count;

/* Write-back bookkeeping (the leap-year flag update) */
static int rtc_writeback_count;
static int rtc_writeback_reg;
static uint8_t rtc_writeback_value;

void cal_$rtc_write_reg(uint16_t offset, uint8_t value)
{
    if (offset == CAL_$RTC_WRITE_DATA_OFFSET) {
        rtc_pending_data = value;
        return;
    }
    if (offset != CAL_$RTC_CONTROL_OFFSET) {
        printf("FAILED\n    write to unexpected RTC offset 0x%02x\n", offset);
        tests_failed++;
        return;
    }

    if (rtc_control_count < MAX_CTRL) {
        rtc_control_log[rtc_control_count] = value;
    }
    rtc_control_count++;

    if ((value & CAL_$RTC_CTL_READ) != 0) {
        rtc_selected = value >> CAL_$RTC_CTL_ADDR_SHIFT;
    }
    if ((value & CAL_$RTC_CTL_WRITE) != 0) {
        rtc_writeback_reg = value >> CAL_$RTC_CTL_ADDR_SHIFT;
        rtc_writeback_value = (uint8_t)(~rtc_pending_data & 0x0F);
        rtc_regs[rtc_writeback_reg] = rtc_writeback_value;
        rtc_writeback_count++;
    }
}

uint8_t cal_$rtc_read_reg(uint16_t offset)
{
    if (offset != CAL_$RTC_READ_DATA_OFFSET) {
        printf("FAILED\n    read from unexpected RTC offset 0x%02x\n", offset);
        tests_failed++;
        return 0;
    }
    if (rtc_selected < 0 || rtc_selected >= MSM5832_NUM_REGS) {
        return 0xFF;
    }
    return (uint8_t)~rtc_regs[rtc_selected];
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

/*
 * The calendar registers are SAU2 hardware (SAU2_CALENDAR_BASE, arch/m68k/sau2/hw.h); on the host
 * ARCH_IO_READ8 / ARCH_IO_WRITE8 call these two hooks, which hand the
 * register offset to the model above.
 */
#define SAU2_CALENDAR_BASE 0x00FFA800u
uint8_t arch_$io_read8(uint32_t addr)
{
    return cal_$rtc_read_reg((uint16_t)(addr - SAU2_CALENDAR_BASE));
}
void arch_$io_write8(uint32_t addr, uint8_t val)
{
    cal_$rtc_write_reg((uint16_t)(addr - SAU2_CALENDAR_BASE), val);
}

#include "../read_cal.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

/*
 * Load the mock chip with a date/time.  leap_flag sets D10 bit 2 and
 * hour_24 sets H10 bit 3, exactly the two flag bits the hardware carries.
 */
static void set_rtc(int year_2digit, int month, int day, int weekday,
                    int hour, int minute, int second, int leap_flag)
{
    memset(rtc_regs, 0, sizeof(rtc_regs));
    rtc_regs[MSM5832_REG_Y10]  = (uint8_t)(year_2digit / 10);
    rtc_regs[MSM5832_REG_Y1]   = (uint8_t)(year_2digit % 10);
    rtc_regs[MSM5832_REG_MO10] = (uint8_t)(month / 10);
    rtc_regs[MSM5832_REG_MO1]  = (uint8_t)(month % 10);
    rtc_regs[MSM5832_REG_D10]  = (uint8_t)((day / 10)
                                 | (leap_flag ? MSM5832_D10_LEAP_FLAG : 0));
    rtc_regs[MSM5832_REG_D1]   = (uint8_t)(day % 10);
    rtc_regs[MSM5832_REG_W]    = (uint8_t)weekday;
    rtc_regs[MSM5832_REG_H10]  = (uint8_t)((hour / 10) | MSM5832_H10_24H_FLAG);
    rtc_regs[MSM5832_REG_H1]   = (uint8_t)(hour % 10);
    rtc_regs[MSM5832_REG_MI10] = (uint8_t)(minute / 10);
    rtc_regs[MSM5832_REG_MI1]  = (uint8_t)(minute % 10);
    rtc_regs[MSM5832_REG_S10]  = (uint8_t)(second / 10);
    rtc_regs[MSM5832_REG_S1]   = (uint8_t)(second % 10);

    rtc_selected = -1;
    rtc_pending_data = 0;
    rtc_control_count = 0;
    rtc_writeback_count = 0;
    rtc_writeback_reg = -1;
    rtc_writeback_value = 0;
}

/* Run the real function against the currently loaded chip state. */
static uint32_t read_cal(clock_t *clock_out)
{
    clock_t clock;
    uint32_t seconds = 0;

    clock.high = 0xDEADBEEFu;
    clock.low = 0xBEEF;
    TIME_$READ_CAL(&clock, &seconds);
    if (clock_out != NULL) {
        *clock_out = clock;
    }
    return seconds;
}

/* Convenience: load a date and read it back in one step. */
static uint32_t read_date(int year_2digit, int month, int day,
                          int hour, int minute, int second)
{
    set_rtc(year_2digit, month, day, 0, hour, minute, second, 0);
    return read_cal(NULL);
}

/* ============================================================================
 * Independent reference computations (deliberately NOT the assembly formulas)
 * ============================================================================ */

static int ref_is_leap(int year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static int ref_days_in_month(int month, int year)
{
    static const int dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && ref_is_leap(year)) {
        return 29;
    }
    return dim[month - 1];
}

/* Seconds from 1980-01-01 00:00:00 to the given Gregorian date/time. */
static uint32_t ref_seconds(int year, int month, int day,
                            int hour, int minute, int second)
{
    uint32_t days = 0;
    int y, m;

    for (y = 1980; y < year; y++) {
        days += (uint32_t)(ref_is_leap(y) ? 366 : 365);
    }
    for (m = 1; m < month; m++) {
        days += (uint32_t)ref_days_in_month(m, year);
    }
    days += (uint32_t)(day - 1);

    return days * 86400u + (uint32_t)hour * 3600u
         + (uint32_t)minute * 60u + (uint32_t)second;
}

/* ticks = seconds * 250000, split the way clock_t stores it. */
static void ref_ticks(uint32_t seconds, uint32_t *high_out, uint16_t *low_out)
{
    uint64_t ticks = (uint64_t)seconds * 250000ULL;
    *high_out = (uint32_t)(ticks >> 16);
    *low_out = (uint16_t)(ticks & 0xFFFF);
}

/* ============================================================================
 * Tests: the hardware protocol
 * ============================================================================ */

/*
 * 0x00E2AE06..0x00E2AE32: assert HOLD, then walk the 13 register addresses
 * from Y10 (0xC5) down to S1 (0x05) with READ + HOLD, then re-assert plain
 * HOLD, and finally (0x00E2AE98) release it.
 */
TEST(control_sequence)
{
    int i;
    static const uint8_t expected_read_controls[MSM5832_NUM_REGS] = {
        0xC5, 0xB5, 0xA5, 0x95, 0x85, 0x75, 0x65,
        0x55, 0x45, 0x35, 0x25, 0x15, 0x05
    };

    set_rtc(85, 6, 17, 1, 14, 35, 9, 0);
    (void)read_cal(NULL);

    /* HOLD + 13 register selects + HOLD + release, no leap write-back */
    ASSERT_EQ(0, rtc_writeback_count);
    ASSERT_EQ(1 + MSM5832_NUM_REGS + 2, rtc_control_count);

    ASSERT_EQ(CAL_$RTC_CTL_HOLD, rtc_control_log[0]);
    for (i = 0; i < MSM5832_NUM_REGS; i++) {
        if (rtc_control_log[1 + i] != expected_read_controls[i]) {
            printf("FAILED\n    control %d = 0x%02x, expected 0x%02x\n",
                   i, rtc_control_log[1 + i], expected_read_controls[i]);
            tests_failed++;
            return;
        }
    }
    ASSERT_EQ(CAL_$RTC_CTL_HOLD, rtc_control_log[1 + MSM5832_NUM_REGS]);
    ASSERT_EQ(0x00, rtc_control_log[2 + MSM5832_NUM_REGS]);
}

/*
 * 0x00E2AE72..0x00E2AE92: when the March-based year contains a February 29,
 * the D10 register is rewritten with bit 2 set (0x81 select, 0x83 strobe,
 * 0x81 release).
 */
TEST(leap_write_back)
{
    /* December 1983 sits in the March 1983 - February 1984 window. */
    set_rtc(83, 12, 25, 0, 12, 0, 0, 0);
    (void)read_cal(NULL);
    ASSERT_EQ(1, rtc_writeback_count);
    ASSERT_EQ(MSM5832_REG_D10, rtc_writeback_reg);
    /* day 25 -> tens 2, plus the leap bit */
    ASSERT_EQ(2 | MSM5832_D10_LEAP_FLAG, rtc_writeback_value);

    /* The three control bytes of the write-back handshake */
    ASSERT_EQ(CAL_$RTC_CTL_ADDR_HOLD(MSM5832_REG_D10),
              rtc_control_log[1 + MSM5832_NUM_REGS + 1]);
    ASSERT_EQ(CAL_$RTC_CTL_WRITE_HOLD(MSM5832_REG_D10),
              rtc_control_log[1 + MSM5832_NUM_REGS + 2]);
    ASSERT_EQ(CAL_$RTC_CTL_ADDR_HOLD(MSM5832_REG_D10),
              rtc_control_log[1 + MSM5832_NUM_REGS + 3]);
}

TEST(no_leap_write_back)
{
    /* March 1984 - February 1985 contains no leap day. */
    set_rtc(84, 6, 1, 0, 0, 0, 0, 0);
    (void)read_cal(NULL);
    ASSERT_EQ(0, rtc_writeback_count);
}

/*
 * 0x00E2AEC8 / 0x00E2AEDE: the day-tens and hour-tens digits are masked with
 * 3, so an already-set leap or 24-hour flag never leaks into the arithmetic.
 */
TEST(flag_bits_are_masked_off)
{
    uint32_t with_flag, without_flag;

    set_rtc(85, 6, 17, 0, 14, 35, 9, 1);   /* D10 bit 2 already set */
    with_flag = read_cal(NULL);

    set_rtc(85, 6, 17, 0, 14, 35, 9, 0);
    without_flag = read_cal(NULL);

    ASSERT_EQ(without_flag, with_flag);
    ASSERT_EQ(ref_seconds(1985, 6, 17, 14, 35, 9), with_flag);
}

/* ============================================================================
 * Tests: the value the function actually returns
 * ============================================================================ */

TEST(seconds_match_gregorian)
{
    struct { int y2, mo, d, h, mi, s; int year; } cases[] = {
        { 80,  3,  1,  0,  0,  0, 1980 },
        { 80, 12, 31, 23, 59, 59, 1980 },
        { 81,  1,  1,  0,  0,  0, 1981 },
        { 84,  2, 29,  6, 15, 30, 1984 },
        { 85,  6, 17, 14, 35,  9, 1985 },
        { 85,  7,  4, 12, 30, 45, 1985 },
        { 89, 12, 31, 23, 59, 59, 1989 },
        { 99,  9,  9,  9,  9,  9, 1999 },
    };
    unsigned n;

    for (n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
        uint32_t got = read_date(cases[n].y2, cases[n].mo, cases[n].d,
                                 cases[n].h, cases[n].mi, cases[n].s);
        uint32_t want = ref_seconds(cases[n].year, cases[n].mo, cases[n].d,
                                    cases[n].h, cases[n].mi, cases[n].s);
        if (got != want) {
            printf("FAILED\n    %04d-%02d-%02d %02d:%02d:%02d -> %u, "
                   "expected %u\n",
                   cases[n].year, cases[n].mo, cases[n].d, cases[n].h,
                   cases[n].mi, cases[n].s, got, want);
            tests_failed++;
            return;
        }
    }
}

TEST(clock_is_seconds_times_250000)
{
    clock_t clock;
    uint32_t seconds, want_high;
    uint16_t want_low;

    set_rtc(85, 6, 17, 0, 14, 35, 9, 0);
    seconds = read_cal(&clock);
    ref_ticks(seconds, &want_high, &want_low);
    ASSERT_EQ(want_high, clock.high);
    ASSERT_EQ(want_low, clock.low);

    /* Again at a much larger value, to exercise the high partial product */
    set_rtc(99, 12, 31, 0, 23, 59, 59, 0);
    seconds = read_cal(&clock);
    ref_ticks(seconds, &want_high, &want_low);
    ASSERT_EQ(want_high, clock.high);
    ASSERT_EQ(want_low, clock.low);
}

TEST(time_of_day_components)
{
    uint32_t base = read_date(85, 6, 15, 0, 0, 0);
    ASSERT_EQ(base + 3600, read_date(85, 6, 15, 1, 0, 0));
    ASSERT_EQ(base + 60,   read_date(85, 6, 15, 0, 1, 0));
    ASSERT_EQ(base + 1,    read_date(85, 6, 15, 0, 0, 1));
    ASSERT_EQ(base + 23u * 3600u + 59u * 60u + 59u,
              read_date(85, 6, 15, 23, 59, 59));
}

TEST(consecutive_days_differ_by_86400)
{
    ASSERT_EQ(86400, read_date(85, 3, 2, 0, 0, 0) - read_date(85, 3, 1, 0, 0, 0));
    /* across a month boundary */
    ASSERT_EQ(86400, read_date(85, 4, 1, 0, 0, 0) - read_date(85, 3, 31, 0, 0, 0));
    /* across a year boundary */
    ASSERT_EQ(86400, read_date(86, 1, 1, 0, 0, 0) - read_date(85, 12, 31, 0, 0, 0));
}

TEST(month_lengths)
{
    static const int lengths_1985[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30
    };
    int m;

    for (m = 1; m <= 11; m++) {
        uint32_t d1 = read_date(85, m, 1, 0, 0, 0);
        uint32_t d2 = read_date(85, m + 1, 1, 0, 0, 0);
        uint32_t gap = (d2 - d1) / 86400u;
        if (gap != (uint32_t)lengths_1985[m - 1]) {
            printf("FAILED\n    1985-%02d -> %02d: %u days, expected %d\n",
                   m, m + 1, gap, lengths_1985[m - 1]);
            tests_failed++;
            return;
        }
    }
    /* February 1984 has 29 days */
    ASSERT_EQ(29, (read_date(84, 3, 1, 0, 0, 0)
                   - read_date(84, 2, 1, 0, 0, 0)) / 86400u);
}

TEST(full_years)
{
    /* March 1980 - March 1981: no February 29 in that window */
    ASSERT_EQ(365, (read_date(81, 3, 1, 0, 0, 0)
                    - read_date(80, 3, 1, 0, 0, 0)) / 86400u);
    /* March 1983 - March 1984 does contain February 29, 1984 */
    ASSERT_EQ(366, (read_date(84, 3, 1, 0, 0, 0)
                    - read_date(83, 3, 1, 0, 0, 0)) / 86400u);
    /* Five years, one of them leap */
    ASSERT_EQ(1826, (read_date(86, 3, 1, 0, 0, 0)
                     - read_date(81, 3, 1, 0, 0, 0)) / 86400u);
}

/*
 * The March-based year counter is normalised into 0..99 (0x00E2AE60), so
 * January and February 1980 - the only dates before the first complete
 * March-based year - wrap to the far end of the 100-year window instead of
 * producing a small number.  This is the original's behaviour, not a defect
 * in the translation; TIME_$INIT only ever sees a chip that has been set by
 * CAL_$WRITE_CALENDAR.
 *
 * 1980-01-01 gives year_from_epoch = 99, total_days = 36525, and
 * (uint16_t)(36525 * 2) * 43200 = 324,604,800.
 */
TEST(days_epoch_wraps_at_the_century)
{
    ASSERT_EQ(324604800u, read_date(80, 1, 1, 0, 0, 0));
    /* An honest Gregorian epoch would be 0 */
    ASSERT_EQ(0u, ref_seconds(1980, 1, 1, 0, 0, 0));
    /* The wrap is uniform: the next day is still exactly 86400 later */
    ASSERT_EQ(86400, read_date(80, 1, 2, 0, 0, 0) - read_date(80, 1, 1, 0, 0, 0));
    /* March 1980 is back in range and agrees with the Gregorian answer */
    ASSERT_EQ(ref_seconds(1980, 3, 1, 0, 0, 0), read_date(80, 3, 1, 0, 0, 0));
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("=== TIME_$READ_CAL tests ===\n\n");

    printf("Hardware protocol:\n");
    RUN_TEST(control_sequence);
    RUN_TEST(leap_write_back);
    RUN_TEST(no_leap_write_back);
    RUN_TEST(flag_bits_are_masked_off);

    printf("\nConversion:\n");
    RUN_TEST(seconds_match_gregorian);
    RUN_TEST(clock_is_seconds_times_250000);
    RUN_TEST(time_of_day_components);
    RUN_TEST(consecutive_days_differ_by_86400);
    RUN_TEST(month_lengths);
    RUN_TEST(full_years);
    RUN_TEST(days_epoch_wraps_at_the_century);

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
