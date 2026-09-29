/*
 * cal/test/test_write_calendar.c - Unit tests for CAL_$WRITE_CALENDAR
 *
 * Self-contained host program: includes cal/write_calendar.c directly and
 * supplies the host implementation of the MSM5832 register ports
 * (cal_$rtc_read_reg / cal_$rtc_write_reg from cal/cal.h), so every byte the
 * driver puts on the control and data ports is recorded in order.
 *
 * The assertions are on the exact 13-register sequence emitted at
 * 0x00E8167C: control 0xC1 (Y10) stepping down by 0x10 to 0x01 (S1), each
 * digit written as ~digit on the data port with a WRITE strobe (control | 2)
 * in between.
 */

#include "cal_test.h"

/* ==========================================================================
 * Mock MSM5832 register ports
 * ========================================================================== */

typedef struct {
    uint16_t offset;
    uint8_t value;
} rtc_write_t;

#define MAX_RTC_WRITES 256
static rtc_write_t rtc_writes[MAX_RTC_WRITES];
static int rtc_write_count;

void cal_$rtc_write_reg(uint16_t offset, uint8_t value)
{
    if (rtc_write_count < MAX_RTC_WRITES) {
        rtc_writes[rtc_write_count].offset = offset;
        rtc_writes[rtc_write_count].value = value;
    }
    rtc_write_count++;
}

uint8_t cal_$rtc_read_reg(uint16_t offset)
{
    (void)offset;
    return 0;
}

static void rtc_reset(void) { rtc_write_count = 0; }

/* ==========================================================================
 * Kernel globals the function under test references
 * ========================================================================== */

int8_t NETWORK_$REALLY_DISKLESS;

/* Implementation under test */
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

#include "../write_calendar.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * The three-write handshake cal_$write_calendar_digit performs for one
 * register, starting at rtc_writes[i]:
 *
 *   data  <- ~digit
 *   ctrl  <- control
 *   ctrl  <- control | CAL_$RTC_CTL_CTL_WRITE
 *   ctrl  <- control
 *
 * Returns the index just past the handshake, or -1 on mismatch (having
 * already recorded the failure).
 */
static int check_digit(int i, uint8_t control, uint8_t digit, const char *what)
{
    struct { uint16_t off; uint8_t val; } want[4] = {
        { CAL_$RTC_WRITE_DATA_OFFSET, (uint8_t)~digit },
        { CAL_$RTC_CONTROL_OFFSET,    control },
        { CAL_$RTC_CONTROL_OFFSET,    (uint8_t)(control | CAL_$RTC_CTL_WRITE) },
        { CAL_$RTC_CONTROL_OFFSET,    control },
    };
    int k;

    for (k = 0; k < 4; k++) {
        if (i + k >= rtc_write_count) {
            printf("FAILED\n    %s: ran out of writes at index %d\n", what, i + k);
            tests_failed++;
            return -1;
        }
        if (rtc_writes[i + k].offset != want[k].off ||
            rtc_writes[i + k].value != want[k].val) {
            printf("FAILED\n    %s: write %d is +0x%02x=0x%02x, expected "
                   "+0x%02x=0x%02x\n",
                   what, i + k, rtc_writes[i + k].offset, rtc_writes[i + k].value,
                   want[k].off, want[k].val);
            tests_failed++;
            return -1;
        }
    }
    return i + 4;
}

/* Drive the function with a full date/time. */
static void write_calendar(int y, int mo, int d, int w, int h, int mi, int s)
{
    int16_t year = (int16_t)y, month = (int16_t)mo, day = (int16_t)d;
    int16_t weekday = (int16_t)w, hour = (int16_t)h;
    int16_t minute = (int16_t)mi, second = (int16_t)s;

    rtc_reset();
    CAL_$WRITE_CALENDAR(&year, &month, &day, &weekday, &hour, &minute, &second);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * The complete sequence for a non-leap-flag date.
 *
 * 1985-06-17 (a Monday), 14:35:09.
 *   year mod 100 = 85, month 6 > 2 so the leap test uses 86; 86 & 3 = 2,
 *   so the day is written unmodified.
 *   hour 14 + 0x50 = 94 -> tens 9 (bit 3 = 24-hour select), ones 4.
 */
TEST(full_sequence_no_leap)
{
    int i = 0;

    write_calendar(1985, 6, 17, 1, 14, 35, 9);

    /* control 0x01 (assert HOLD) + 13 * 4 register writes + control 0x00 */
    ASSERT_EQ(rtc_write_count, 1 + 13 * 4 + 1);

    ASSERT_EQ(rtc_writes[0].offset, CAL_$RTC_CONTROL_OFFSET);
    ASSERT_EQ(rtc_writes[0].value, CAL_$RTC_CTL_HOLD);
    i = 1;

    i = check_digit(i, 0xC1, 8, "Y10");   if (i < 0) return;  /* 85 / 10 */
    i = check_digit(i, 0xB1, 5, "Y1");    if (i < 0) return;
    i = check_digit(i, 0xA1, 0, "MO10");  if (i < 0) return;  /* 6 */
    i = check_digit(i, 0x91, 6, "MO1");   if (i < 0) return;
    i = check_digit(i, 0x81, 1, "D10");   if (i < 0) return;  /* 17 */
    i = check_digit(i, 0x71, 7, "D1");    if (i < 0) return;
    i = check_digit(i, 0x61, 1, "W");     if (i < 0) return;
    i = check_digit(i, 0x51, 9, "H10");   if (i < 0) return;  /* (14+80)/10 */
    i = check_digit(i, 0x41, 4, "H1");    if (i < 0) return;
    i = check_digit(i, 0x31, 3, "MI10");  if (i < 0) return;  /* 35 */
    i = check_digit(i, 0x21, 5, "MI1");   if (i < 0) return;
    i = check_digit(i, 0x11, 0, "S10");   if (i < 0) return;  /* 9 */
    i = check_digit(i, 0x01, 9, "S1");    if (i < 0) return;

    ASSERT_EQ(rtc_writes[i].offset, CAL_$RTC_CONTROL_OFFSET);
    ASSERT_EQ(rtc_writes[i].value, 0x00);
}

/*
 * The control byte must step down by exactly one MSM5832 register address per
 * digit.  This is the defect the audit found: the pre-fix C hardcoded a
 * per-field constant and got 11 of the 13 registers wrong.
 */
TEST(control_bytes_step_by_one_register)
{
    static const uint8_t expected[13] = {
        0xC1, 0xB1, 0xA1, 0x91, 0x81, 0x71, 0x61,
        0x51, 0x41, 0x31, 0x21, 0x11, 0x01
    };
    int i, d;

    write_calendar(1985, 6, 17, 1, 14, 35, 9);

    for (d = 0; d < 13; d++) {
        i = 1 + d * 4 + 1;   /* the "control <- address+HOLD" write */
        ASSERT_EQ(rtc_writes[i].offset, CAL_$RTC_CONTROL_OFFSET);
        if (rtc_writes[i].value != expected[d]) {
            printf("FAILED\n    digit %d control = 0x%02x, expected 0x%02x\n",
                   d, rtc_writes[i].value, expected[d]);
            tests_failed++;
            return;
        }
    }
}

/*
 * Every one of the 13 registers is written, seconds-ones (address 0)
 * included.  The pre-fix C stopped one register short.
 */
TEST(seconds_ones_is_written)
{
    int i = 1 + 12 * 4;   /* the 13th digit handshake */

    write_calendar(1985, 6, 17, 1, 14, 35, 9);

    ASSERT_EQ(check_digit(i, 0x01, 9, "S1"), i + 4);
}

/*
 * 0x00E816D6: the leap flag lands in bit 3 of the date-tens digit (+0x50).
 *
 * ORIGINAL BUG (bead source-tcxm, resolved): the MSM5832 keeps its leap-year
 * flag in D10 bit 2 and leaves D10 bit 3 unused, so this write never programs
 * the chip's leap counter.  The test pins the behaviour as found: bit 3 set,
 * bit 2 clear.
 */
TEST(leap_flag_sets_bit3_of_day_tens)
{
    uint8_t d10;

    /* 1983-06-17: month > 2 -> test year 84; 84 & 3 == 0 -> leap. */
    write_calendar(1983, 6, 17, 1, 14, 35, 9);
    /* day 17 + 0x50 = 97 -> tens 9 (0b1001: tens 1 plus bit 3), ones 7 */
    ASSERT_EQ(check_digit(1 + 4 * 4, 0x81, 9, "D10 leap"), 1 + 5 * 4);
    ASSERT_EQ(check_digit(1 + 5 * 4, 0x71, 7, "D1 leap"), 1 + 6 * 4);

    /* The data port is written inverted; recover the digit the chip sees. */
    d10 = (uint8_t)(~rtc_writes[1 + 4 * 4].value & 0x0f);
    ASSERT_EQ(d10 & MSM5832_D10_UNUSED, MSM5832_D10_UNUSED);
    ASSERT_EQ(d10 & MSM5832_D10_LEAP_FLAG, 0);
    ASSERT_EQ(d10 & MSM5832_TENS_MASK, 1);   /* date tens of 17 */
}

/* January/February use the current year for the leap test (month <= 2). */
TEST(leap_test_uses_current_year_in_jan_feb)
{
    /* 1984-02-17: month 2, so the test year stays 84 -> 84 & 3 == 0, leap. */
    write_calendar(1984, 2, 17, 1, 14, 35, 9);
    ASSERT_EQ(check_digit(1 + 4 * 4, 0x81, 9, "D10 feb-leap"), 1 + 5 * 4);

    /* 1983-02-17: test year 83, 83 & 3 == 3 -> no flag. */
    write_calendar(1983, 2, 17, 1, 14, 35, 9);
    ASSERT_EQ(check_digit(1 + 4 * 4, 0x81, 1, "D10 feb-noleap"), 1 + 5 * 4);
}

/* March..December use year + 1 for the leap test. */
TEST(leap_test_uses_next_year_from_march)
{
    /* 1984-03-01: month > 2 -> test year 85; 85 & 3 == 1 -> no flag. */
    write_calendar(1984, 3, 1, 1, 0, 0, 0);
    ASSERT_EQ(check_digit(1 + 4 * 4, 0x81, 0, "D10 mar-1984"), 1 + 5 * 4);

    /* 1987-03-01: test year 88; 88 & 3 == 0 -> flag. */
    write_calendar(1987, 3, 1, 1, 0, 0, 0);
    ASSERT_EQ(check_digit(1 + 4 * 4, 0x81, 8, "D10 mar-1987"), 1 + 5 * 4);
}

/* 0x00E816EA: the hour always carries the 24-hour select in H10 bit 3. */
TEST(hour_tens_carries_24h_flag)
{
    write_calendar(1985, 6, 17, 1, 23, 0, 0);
    /* 23 + 80 = 103 -> tens 10 (0b1010: tens 2 plus bit 3), ones 3 */
    ASSERT_EQ(check_digit(1 + 7 * 4, 0x51, 10, "H10 23h"), 1 + 8 * 4);
    ASSERT_EQ(check_digit(1 + 8 * 4, 0x41, 3, "H1 23h"), 1 + 9 * 4);

    write_calendar(1985, 6, 17, 1, 0, 0, 0);
    /* 0 + 80 = 80 -> tens 8 (bit 3 only), ones 0 */
    ASSERT_EQ(check_digit(1 + 7 * 4, 0x51, 8, "H10 0h"), 1 + 8 * 4);
    ASSERT_EQ(check_digit(1 + 8 * 4, 0x41, 0, "H1 0h"), 1 + 9 * 4);
}

/* Only year mod 100 reaches the chip (0x00E816A6 divu.w #0x64 / swap). */
TEST(year_is_reduced_mod_100)
{
    write_calendar(2003, 1, 1, 1, 0, 0, 0);
    ASSERT_EQ(check_digit(1, 0xC1, 0, "Y10 2003"), 5);
    ASSERT_EQ(check_digit(5, 0xB1, 3, "Y1 2003"), 9);
}

/* The weekday occupies a single register (W, address 6). */
TEST(weekday_is_one_register)
{
    write_calendar(1985, 6, 17, 5, 0, 0, 0);
    ASSERT_EQ(check_digit(1 + 6 * 4, 0x61, 5, "W"), 1 + 7 * 4);
}

/* 0x00E81680: a diskless node touches the ports not at all. */
TEST(diskless_writes_nothing)
{
    NETWORK_$REALLY_DISKLESS = 1;
    write_calendar(1985, 6, 17, 1, 14, 35, 9);
    NETWORK_$REALLY_DISKLESS = 0;
    ASSERT_EQ(rtc_write_count, 0);
}

/* The data port always receives the one's complement of the digit. */
TEST(data_is_inverted)
{
    int i;
    write_calendar(1985, 6, 17, 1, 14, 35, 9);
    for (i = 1; i < rtc_write_count - 1; i += 4) {
        ASSERT_EQ(rtc_writes[i].offset, CAL_$RTC_WRITE_DATA_OFFSET);
        /* the complement of a 0..15 digit always has its high nibble set */
        ASSERT_EQ(rtc_writes[i].value & 0xF0, 0xF0);
    }
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("CAL_$WRITE_CALENDAR tests\n");

    RUN_TEST(full_sequence_no_leap);
    RUN_TEST(control_bytes_step_by_one_register);
    RUN_TEST(seconds_ones_is_written);
    RUN_TEST(leap_flag_sets_bit3_of_day_tens);
    RUN_TEST(leap_test_uses_current_year_in_jan_feb);
    RUN_TEST(leap_test_uses_next_year_from_march);
    RUN_TEST(hour_tens_carries_24h_flag);
    RUN_TEST(year_is_reduced_mod_100);
    RUN_TEST(weekday_is_one_register);
    RUN_TEST(diskless_writes_nothing);
    RUN_TEST(data_is_inverted);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
