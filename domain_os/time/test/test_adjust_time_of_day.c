/*
 * time/test/test_adjust_time_of_day.c - Unit tests for
 * TIME_$ADJUST_TIME_OF_DAY (0x00E168DE) and TIME_$GET_ADJUST (0x00E16AA8)
 *
 * The real time/adjust_time_of_day.c and time/get_adjust.c are #included,
 * together with the real math/mult.c and math/mod.c runtime routines they
 * call (M$MIS$LLL, M$OIS$WLW, ...), so the rounding and sign behaviour comes
 * from the same code the kernel uses; the two signed divides are supplied
 * locally (see the note above their definitions).  The CAL_ callees and
 * TIME_$GET_TIME_OF_DAY are mocked and record what they were handed.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "time/time_internal.h"
#include "math/math.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx (%lld), Got: 0x%llx (%lld) at line %d\n", \
                   (unsigned long long)_e, _e, (unsigned long long)_a, _a,    \
                   __LINE__);                                                 \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

int __host_intr_disable_count = 0;

uint16_t TIME_$CURRENT_TICK;
uint16_t TIME_$CURRENT_SKEW;
uint32_t TIME_$CURRENT_DELTA;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static uint32_t mock_tv_sec;
static uint32_t mock_tv_usec;
static int gtod_calls;

void TIME_$GET_TIME_OF_DAY(uint32_t *tv)
{
    gtod_calls++;
    tv[0] = mock_tv_sec;
    tv[1] = mock_tv_usec;
}

static int sec_to_clock_calls;
static uint32_t sec_to_clock_arg;

/* Stand-in: encodes the seconds in `high` so the later ADD48 is visible. */
void CAL_$SEC_TO_CLOCK(uint *sec, clock_t *clock_ret)
{
    sec_to_clock_calls++;
    sec_to_clock_arg = *sec;
    clock_ret->high = *sec;
    clock_ret->low = 0;
}

/* Real 48-bit add, reproduced so the test stays one translation unit. */
void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t low = (uint32_t)dst->low + (uint32_t)src->low;
    dst->low = (uint16_t)low;
    dst->high = dst->high + src->high + (low >> 16);
}

static int decode_calls;
static clock_t decode_clock;

void CAL_$DECODE_TIME(clock_t *clock, short *time_rec)
{
    decode_calls++;
    decode_clock = *clock;
    time_rec[0] = 1990;
    time_rec[1] = 6;
    time_rec[2] = 15;
    time_rec[3] = 12;
    time_rec[4] = 30;
    time_rec[5] = 45;
}

static int weekday_calls;
static short weekday_args[3];

short CAL_$WEEKDAY(short *year, short *month, short *day)
{
    weekday_calls++;
    weekday_args[0] = *year;
    weekday_args[1] = *month;
    weekday_args[2] = *day;
    return 5;
}

static int write_cal_calls;
static int16_t write_cal_args[7];

void CAL_$WRITE_CALENDAR(int16_t *year, int16_t *month, int16_t *day,
                         int16_t *weekday, int16_t *hour, int16_t *minute,
                         int16_t *second)
{
    write_cal_calls++;
    write_cal_args[0] = *year;
    write_cal_args[1] = *month;
    write_cal_args[2] = *day;
    write_cal_args[3] = *weekday;
    write_cal_args[4] = *hour;
    write_cal_args[5] = *minute;
    write_cal_args[6] = *second;
}

static void reset(void)
{
    __host_intr_disable_count = 0;
    TIME_$CURRENT_TICK = 0x1047;
    TIME_$CURRENT_SKEW = 0;
    TIME_$CURRENT_DELTA = 0;
    mock_tv_sec = 0x12CEA600 + 1000;
    mock_tv_usec = 0;
    gtod_calls = 0;
    sec_to_clock_calls = 0;
    sec_to_clock_arg = 0;
    decode_calls = 0;
    weekday_calls = 0;
    write_cal_calls = 0;
    memset(write_cal_args, 0, sizeof(write_cal_args));
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "math/mult.c"
#include "math/mod.c"

/*
 * math/div.c's M$DIS$LLL is not usable on the host (its decompiled body
 * returns garbage for e.g. 500050 / 250000), so the two signed divides are
 * supplied here with the Pascal `div` semantics the runtime implements:
 * truncation toward zero.
 */
long M$DIS$LLL(long dividend, long divisor)
{
    return (long)((int32_t)dividend / (int32_t)divisor);
}

long M$DIS$LLW(long dividend, short divisor)
{
    return (long)((int32_t)dividend / (int32_t)divisor);
}

#include "../adjust_time_of_day.c"
#include "../get_adjust.c"

/* ==========================================================================
 * TIME_$ADJUST_TIME_OF_DAY
 * ========================================================================== */

/* |seconds| above 8000 fails before anything is touched. */
TEST(adjust_rejects_out_of_range)
{
    int32_t delta[2] = { 8001, 0 };
    int32_t old[2] = { 0x5555, 0x6666 };
    status_$t status = 0x11111111;

    reset();
    TIME_$CURRENT_DELTA = 12345;
    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$time_adjustment_out_of_range, status);
    ASSERT_EQ(0x000D000C, status);
    ASSERT_EQ(0, gtod_calls);
    ASSERT_EQ(0, write_cal_calls);
    ASSERT_EQ(12345, TIME_$CURRENT_DELTA);
    ASSERT_EQ(0x1047, TIME_$CURRENT_TICK);
    ASSERT_EQ(0x5555, old[0]);
    ASSERT_EQ(0x6666, old[1]);

    delta[0] = -8001;
    status = 0x11111111;
    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);
    ASSERT_EQ(status_$time_adjustment_out_of_range, status);
    ASSERT_EQ(0, gtod_calls);
}

/* The bound is unsigned: -2^31 negates to itself and is rejected. */
TEST(adjust_rejects_int_min)
{
    int32_t delta[2] = { (int32_t)0x80000000, 0 };
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$time_adjustment_out_of_range, status);
    ASSERT_EQ(0, gtod_calls);
}

/* Exactly 8000 seconds is accepted (bls = lower or same). */
TEST(adjust_accepts_bound)
{
    int32_t delta[2] = { 8000, 0 };
    int32_t old[2] = { 0, 0 };
    status_$t status = 0x11111111;

    reset();
    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, gtod_calls);
    /* 8000 * 250000 = 2e9 > 250000 -> fast step; 2e9 = 1197604 * 1670 + 1320 */
    ASSERT_EQ(0x0686, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x1047 + 0x0686, TIME_$CURRENT_TICK);
    ASSERT_EQ(1197604 * 1670, (int32_t)TIME_$CURRENT_DELTA);
}

/* A zero delta clears the skew, leaves the time of day alone, but still
 * rewrites the calendar and returns the old delta. */
TEST(adjust_zero_delta)
{
    int32_t delta[2] = { 0, 0 };
    int32_t old[2] = { 0, 0 };
    status_$t status = 0x11111111;

    reset();
    TIME_$CURRENT_SKEW = 0xA7;
    TIME_$CURRENT_TICK = 0x10EE;
    TIME_$CURRENT_DELTA = 500050;       /* 2 s + 50 ticks */
    mock_tv_sec = 0x12CEA600 + 777;
    mock_tv_usec = 123456;

    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x1047, TIME_$CURRENT_TICK);
    ASSERT_EQ(0, TIME_$CURRENT_DELTA);
    ASSERT_EQ(2, old[0]);
    ASSERT_EQ(200, old[1]);

    /* time of day unchanged: seconds rebased to the Apollo epoch */
    ASSERT_EQ(1, sec_to_clock_calls);
    ASSERT_EQ(777, sec_to_clock_arg);
    /* 123456 / 4 = 30864 = 0x7890 ticks added through ADD48 */
    ASSERT_EQ(1, decode_calls);
    ASSERT_EQ(777, decode_clock.high);
    ASSERT_EQ(0x7890, decode_clock.low);

    ASSERT_EQ(1, weekday_calls);
    ASSERT_EQ(1990, weekday_args[0]);
    ASSERT_EQ(6, weekday_args[1]);
    ASSERT_EQ(15, weekday_args[2]);

    ASSERT_EQ(1, write_cal_calls);
    ASSERT_EQ(1990, write_cal_args[0]);
    ASSERT_EQ(6, write_cal_args[1]);
    ASSERT_EQ(15, write_cal_args[2]);
    ASSERT_EQ(5, write_cal_args[3]);    /* CAL_$WEEKDAY's result */
    ASSERT_EQ(12, write_cal_args[4]);
    ASSERT_EQ(30, write_cal_args[5]);
    ASSERT_EQ(45, write_cal_args[6]);

    /* the SR bracket is a bare raise / forced IPL 0 */
    ASSERT_EQ(0, __host_intr_disable_count);
}

/* Small positive delta: slow step, rounded toward zero to a multiple. */
TEST(adjust_small_positive_rounds_down)
{
    int32_t delta[2] = { 0, 1000 };     /* 250 ticks */
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    mock_tv_sec = 0x12CEA600 + 10;
    mock_tv_usec = 5000;

    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x00A7, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x10EE, TIME_$CURRENT_TICK);
    ASSERT_EQ(167, TIME_$CURRENT_DELTA);        /* 250 -> 1 * 167 */
    ASSERT_EQ(0, old[0]);
    ASSERT_EQ(0, old[1]);

    /* the RAW delta (not the rounded one) is added to the time of day */
    ASSERT_EQ(10, sec_to_clock_arg);
    ASSERT_EQ(6000 / 4, decode_clock.low);
}

/* Large positive delta: fast step. */
TEST(adjust_large_positive)
{
    int32_t delta[2] = { 2, 0 };        /* 500000 ticks */
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x0686, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x16CD, TIME_$CURRENT_TICK);
    ASSERT_EQ(499330, TIME_$CURRENT_DELTA);     /* 299 * 1670 */
    ASSERT_EQ(1002, sec_to_clock_arg);
}

/* Exactly one second uses the slow step (unsigned "lower or same"), and
 * 250000 is not a multiple of 167. */
TEST(adjust_negative_one_second)
{
    int32_t delta[2] = { -1, 0 };
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    TIME_$CURRENT_DELTA = (uint32_t)-250050;    /* -1 s - 50 ticks */
    mock_tv_sec = 0x12CEA600 + 100;
    mock_tv_usec = 400000;

    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ((uint16_t)-167, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x1047 - 0xA7, TIME_$CURRENT_TICK);
    ASSERT_EQ(-249999, (int32_t)TIME_$CURRENT_DELTA);  /* 1497 * -167 */
    ASSERT_EQ(-1, old[0]);
    ASSERT_EQ(-200, old[1]);

    /* seconds moved back one, microseconds untouched: 100000 = 0x186A0 ticks */
    ASSERT_EQ(99, sec_to_clock_arg);
    ASSERT_EQ(99 + 1, decode_clock.high);
    ASSERT_EQ(0x86A0, decode_clock.low);
}

/* A delta whose tick count rounds to zero: skew 0, DELTA 0, and the time
 * of day is NOT adjusted because the rounded value is what is tested. */
TEST(adjust_rounds_to_zero_skips_time_of_day)
{
    int32_t delta[2] = { 0, -4 };       /* -1 tick */
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    mock_tv_sec = 0x12CEA600 + 50;
    mock_tv_usec = 8;

    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x1047, TIME_$CURRENT_TICK);
    ASSERT_EQ(0, TIME_$CURRENT_DELTA);
    ASSERT_EQ(50, sec_to_clock_arg);
    ASSERT_EQ(2, decode_clock.low);
    ASSERT_EQ(1, write_cal_calls);
}

/* Microsecond overflow carries into the seconds. */
TEST(adjust_normalises_usec_overflow)
{
    int32_t delta[2] = { 0, 4000 };     /* 1000 ticks -> 5 * 167 = 835 */
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    mock_tv_sec = 0x12CEA600 + 20;
    mock_tv_usec = 999000;

    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(835, TIME_$CURRENT_DELTA);
    ASSERT_EQ(21, sec_to_clock_arg);
    ASSERT_EQ(3000 / 4, decode_clock.low);
}

/* Exactly 1000000 microseconds also carries (bgt, not bge). */
TEST(adjust_normalises_usec_exactly_one_second)
{
    int32_t delta[2] = { 0, 1000 };
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    mock_tv_sec = 0x12CEA600 + 20;
    mock_tv_usec = 999000;

    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    ASSERT_EQ(21, sec_to_clock_arg);
    ASSERT_EQ(0, decode_clock.low);
}

/* Negative microseconds borrow from the seconds; the 32-bit tick count is
 * split across the 6-byte record (word 0, then a longword). */
TEST(adjust_normalises_usec_underflow)
{
    int32_t delta[2] = { -1, -500000 };
    int32_t old[2] = { 0, 0 };
    status_$t status = 0;

    reset();
    mock_tv_sec = 0x12CEA600 + 100;
    mock_tv_usec = 100000;

    TIME_$ADJUST_TIME_OF_DAY(delta, old, &status);

    /* -250000 - 125000 = -375000 -> fast step, 224 * -1670 */
    ASSERT_EQ((uint16_t)-1670, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x1047 - 0x686, TIME_$CURRENT_TICK);
    ASSERT_EQ(-374080, (int32_t)TIME_$CURRENT_DELTA);

    /* 100 - 1 - 1 (borrow) = 98; usec = 600000 -> 150000 = 0x249F0 ticks */
    ASSERT_EQ(98, sec_to_clock_arg);
    ASSERT_EQ(98 + 2, decode_clock.high);
    ASSERT_EQ(0x49F0, decode_clock.low);
}

/* ==========================================================================
 * TIME_$GET_ADJUST
 * ========================================================================== */

TEST(get_adjust_positive)
{
    int32_t out[2] = { 0, 0 };

    reset();
    TIME_$CURRENT_DELTA = 3 * 250000 + 1234;
    __host_intr_disable_count = 5;   /* the exit forces IPL 0 */

    TIME_$GET_ADJUST(out);

    ASSERT_EQ(3, out[0]);
    ASSERT_EQ(1234 * 4, out[1]);
    ASSERT_EQ(0, __host_intr_disable_count);
}

TEST(get_adjust_negative)
{
    int32_t out[2] = { 0, 0 };

    reset();
    TIME_$CURRENT_DELTA = (uint32_t)(-(2 * 250000) - 7);

    TIME_$GET_ADJUST(out);

    ASSERT_EQ(-2, out[0]);
    ASSERT_EQ(-28, out[1]);
}

int main(void)
{
    printf("test_adjust_time_of_day:\n");

    RUN_TEST(adjust_rejects_out_of_range);
    RUN_TEST(adjust_rejects_int_min);
    RUN_TEST(adjust_accepts_bound);
    RUN_TEST(adjust_zero_delta);
    RUN_TEST(adjust_small_positive_rounds_down);
    RUN_TEST(adjust_large_positive);
    RUN_TEST(adjust_negative_one_second);
    RUN_TEST(adjust_rounds_to_zero_skips_time_of_day);
    RUN_TEST(adjust_normalises_usec_overflow);
    RUN_TEST(adjust_normalises_usec_exactly_one_second);
    RUN_TEST(adjust_normalises_usec_underflow);
    RUN_TEST(get_adjust_positive);
    RUN_TEST(get_adjust_negative);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
