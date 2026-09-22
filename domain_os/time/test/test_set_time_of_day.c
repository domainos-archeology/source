/*
 * time/test/test_set_time_of_day.c - Unit tests for TIME_$SET_TIME_OF_DAY
 * (0x00E1678C)
 *
 * The real time/set_time_of_day.c is #included; CAL_$SEC_TO_CLOCK, ADD48,
 * TIME_$ABS_CLOCK and the calendar writers are mocked.  The cases pin the
 * 16-bit elapsed-tick subtract (abs.low - TIME_$CLOCKL wraps as a word),
 * the borrow into TIME_$CURRENT_CLOCKH, the microsecond underflow, the
 * BOOT_TIME re-base, and the pre-epoch shortcut.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "time/time_internal.h"

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
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

int __host_intr_disable_count = 0;

uint16_t TIME_$CLOCKL;
uint32_t TIME_$CURRENT_CLOCKH;
uint16_t TIME_$CURRENT_CLOCKL;
uint32_t TIME_$BOOT_TIME;
uint32_t TIME_$CURRENT_TIME;
uint32_t TIME_$CURRENT_USEC;

/* Stand-in: seconds go into `high`, so the ADD48 of the usec ticks shows. */
static uint32_t sec_to_clock_arg;
void CAL_$SEC_TO_CLOCK(uint *sec, clock_t *clock_ret)
{
    sec_to_clock_arg = *sec;
    clock_ret->high = *sec;
    clock_ret->low = 0;
}

void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t low = (uint32_t)dst->low + (uint32_t)src->low;
    dst->low = (uint16_t)low;
    dst->high = dst->high + src->high + (low >> 16);
}

static clock_t mock_abs;
static int abs_ipl;
void TIME_$ABS_CLOCK(clock_t *clock)
{
    abs_ipl = __host_intr_disable_count;
    *clock = mock_abs;
}

static clock_t decode_clock;
void CAL_$DECODE_TIME(clock_t *clock, short *time_rec)
{
    decode_clock = *clock;
    time_rec[0] = 1991; time_rec[1] = 2; time_rec[2] = 3;
    time_rec[3] = 4; time_rec[4] = 5; time_rec[5] = 6;
}

short CAL_$WEEKDAY(short *year, short *month, short *day)
{
    (void)year; (void)month; (void)day;
    return 2;
}

static int write_cal_calls;
static int16_t write_cal_args[7];
void CAL_$WRITE_CALENDAR(int16_t *year, int16_t *month, int16_t *day,
                         int16_t *weekday, int16_t *hour, int16_t *minute,
                         int16_t *second)
{
    write_cal_calls++;
    write_cal_args[0] = *year; write_cal_args[1] = *month;
    write_cal_args[2] = *day; write_cal_args[3] = *weekday;
    write_cal_args[4] = *hour; write_cal_args[5] = *minute;
    write_cal_args[6] = *second;
}

#include "../set_time_of_day.c"

static void reset(void)
{
    __host_intr_disable_count = 0;
    TIME_$CLOCKL = 0;
    TIME_$CURRENT_CLOCKH = 0;
    TIME_$CURRENT_CLOCKL = 0;
    TIME_$BOOT_TIME = 0;
    TIME_$CURRENT_TIME = 0;
    TIME_$CURRENT_USEC = 0;
    mock_abs = (clock_t){ 0, 0 };
    abs_ipl = -1;
    write_cal_calls = 0;
    sec_to_clock_arg = 0;
}

TEST(pre_epoch_zeroes_clock)
{
    uint32_t tv[2] = { 0x12CEA5FF, 123457 };
    status_$t status = 0x5555;

    reset();
    TIME_$CURRENT_CLOCKH = 0x77; TIME_$CURRENT_CLOCKL = 0x88;
    TIME_$SET_TIME_OF_DAY(tv, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, TIME_$CURRENT_CLOCKH);
    ASSERT_EQ(0, TIME_$CURRENT_CLOCKL);
    ASSERT_EQ(0x12CEA5FF, TIME_$CURRENT_TIME);
    ASSERT_EQ(123456, TIME_$CURRENT_USEC);          /* & ~3 */
    ASSERT_EQ(0, write_cal_calls);
    ASSERT_EQ(-1, abs_ipl);                         /* never sampled */
}

TEST(plain_set_with_elapsed_ticks)
{
    uint32_t tv[2] = { 0x12CEA600 + 100, 4000 };    /* 1000 ticks */
    status_$t status = 0;

    reset();
    TIME_$CLOCKL = 0x10;
    mock_abs = (clock_t){ 9, 0x18 };                /* 8 ticks elapsed */
    TIME_$CURRENT_CLOCKH = 0;                       /* no BOOT_TIME re-base */

    TIME_$SET_TIME_OF_DAY(tv, &status);

    ASSERT_EQ(100, sec_to_clock_arg);
    ASSERT_EQ(1, abs_ipl);                          /* sampled at IPL 7 */
    ASSERT_EQ(0, __host_intr_disable_count);        /* forced back to 0 */
    ASSERT_EQ(100, TIME_$CURRENT_CLOCKH);           /* new_clock.high */
    ASSERT_EQ(1000 - 8, TIME_$CURRENT_CLOCKL);
    ASSERT_EQ(0x12CEA600 + 100, TIME_$CURRENT_TIME);
    ASSERT_EQ(4000 - 32, TIME_$CURRENT_USEC);
    ASSERT_EQ(0, TIME_$BOOT_TIME);
    /* the calendar is written from new_clock (100 s + 1000 ticks) */
    ASSERT_EQ(100, decode_clock.high);
    ASSERT_EQ(1000, decode_clock.low);
    ASSERT_EQ(1, write_cal_calls);
    ASSERT_EQ(1991, write_cal_args[0]);
    ASSERT_EQ(2, write_cal_args[3]);                /* CAL_$WEEKDAY's result */
    ASSERT_EQ(6, write_cal_args[6]);
}

TEST(boot_time_rebased_when_clock_running)
{
    uint32_t tv[2] = { 0x12CEA600 + 500, 0 };
    status_$t status = 0;

    reset();
    TIME_$CURRENT_CLOCKH = 200;
    TIME_$BOOT_TIME = 50;
    TIME_$SET_TIME_OF_DAY(tv, &status);

    ASSERT_EQ(50 + (500 - 200), TIME_$BOOT_TIME);
    ASSERT_EQ(500, TIME_$CURRENT_CLOCKH);
}

/* abs.low < TIME_$CLOCKL: the elapsed count is a WORD subtract (wraps). */
TEST(elapsed_is_sixteen_bit)
{
    uint32_t tv[2] = { 0x12CEA600 + 1, 0 };
    status_$t status = 0;

    reset();
    TIME_$CLOCKL = 0xFFF0;
    mock_abs = (clock_t){ 3, 0x0004 };              /* 0x0004 - 0xFFF0 = 0x14 */

    TIME_$SET_TIME_OF_DAY(tv, &status);

    /* new.low 0 - 0x14 < 0: borrow from CURRENT_CLOCKH, low wraps */
    ASSERT_EQ(0, TIME_$CURRENT_CLOCKH);             /* 1 - 1 */
    ASSERT_EQ((uint16_t)(0 - 0x14), TIME_$CURRENT_CLOCKL);
    /* usec 0 - 0x14*4 < 0: + 1000000 and one second borrowed */
    ASSERT_EQ(1000000 - 0x50, TIME_$CURRENT_USEC);
    ASSERT_EQ(0x12CEA600 + 1 - 1, TIME_$CURRENT_TIME);
}

/* Negative microseconds divide toward zero and the masked usecs keep bit 31. */
TEST(negative_usecs_round_toward_zero)
{
    uint32_t tv[2] = { 0x12CEA600 + 7, (uint32_t)-5 };  /* -5 / 4 = -1 */
    status_$t status = 0;

    reset();
    TIME_$SET_TIME_OF_DAY(tv, &status);

    /* new_clock = {7, 0} + {0xFFFF, 0xFFFF} (the -1 tick straddling high/low) */
    ASSERT_EQ(7 + 0xFFFF, decode_clock.high);
    ASSERT_EQ(0xFFFF, decode_clock.low);
    /* usecs = -5 & ~3 = -8, no elapsed ticks: negative -> +1000000, second borrowed */
    ASSERT_EQ(1000000 - 8, TIME_$CURRENT_USEC);
    ASSERT_EQ(0x12CEA600 + 6, TIME_$CURRENT_TIME);
}

int main(void)
{
    printf("test_set_time_of_day:\n");
    RUN_TEST(pre_epoch_zeroes_clock);
    RUN_TEST(plain_set_with_elapsed_ticks);
    RUN_TEST(boot_time_rebased_when_clock_running);
    RUN_TEST(elapsed_is_sixteen_bit);
    RUN_TEST(negative_usecs_round_toward_zero);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
