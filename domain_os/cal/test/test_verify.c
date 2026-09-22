/*
 * cal/test/test_verify.c - Unit tests for CAL_$VERIFY (0x00E68380)
 *
 * The real cal/verify.c and cal/cal_data.c are #included; CAL_$READ_TIMEZONE,
 * CRASH_SYSTEM, VFMT_$WRITE10 and TERM_$READ are mocked.  TERM_$READ is fed
 * a scripted sequence of answers so the Y/N loop can be walked.
 */

#include "cal_test.h"

#include <stdarg.h>

#include "misc/crash_system.h"
#include "term/term.h"
#include "vfmt/vfmt.h"

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

uint32_t TIME_$CLOCKH;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int read_tz_calls;
static status_$t read_tz_status_value;

void CAL_$READ_TIMEZONE(cal_$timezone_rec_t *tz_out, status_$t *status)
{
    read_tz_calls++;
    *tz_out = CAL_$TIMEZONE;
    *status = read_tz_status_value;
}

static int crash_calls;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
}

#define MAX_WRITES 8
static int write_calls;
static const char *write_fmt[MAX_WRITES];
static const void *write_arg[MAX_WRITES];

void VFMT_$WRITE10(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    if (write_calls < MAX_WRITES) {
        write_fmt[write_calls] = format;
        write_arg[write_calls] = va_arg(ap, const void *);
    }
    va_end(ap);
    write_calls++;
}

static const char *answers;         /* one character per TERM_$READ */
static int read_calls;
static short read_line;
static short read_limit;

unsigned short TERM_$READ(short *line_ptr, void *buffer, void *param3,
                          status_$t *status_ret)
{
    read_line = *line_ptr;
    read_limit = *(short *)param3;
    ((char *)buffer)[0] = answers[read_calls];
    read_calls++;
    *status_ret = status_$ok;
    return 1;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../cal_data.c"
#include "../verify.c"

static void reset(void)
{
    memset(&CAL_$TIMEZONE, 0, sizeof(CAL_$TIMEZONE));
    CAL_$TIMEZONE.drift.high = 0x11;
    CAL_$TIMEZONE.drift.low = 0x22;
    CAL_$LAST_VALID_TIME = 1000;
    TIME_$CLOCKH = 1000;
    read_tz_calls = 0;
    read_tz_status_value = status_$ok;
    crash_calls = 0;
    write_calls = 0;
    read_calls = 0;
    answers = "";
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

TEST(in_range_is_true_and_clears_drift)
{
    int max_delta = 50;
    char interactive = (char)0xFF;
    status_$t status = 0x5555;
    char r;

    reset();
    TIME_$CLOCKH = 1050;                /* delta == max, ble passes */

    r = CAL_$VERIFY(&max_delta, NULL, &interactive, &status);

    ASSERT_EQ((uint8_t)r, 0xFF);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(read_tz_calls, 1);
    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0);
    ASSERT_EQ(write_calls, 0);
    ASSERT_EQ(crash_calls, 0);
}

TEST(slightly_behind_is_still_true)
{
    int max_delta = 0;
    char interactive = (char)0xFF;
    status_$t status = 0;
    char r;

    reset();
    TIME_$CLOCKH = 1000 - 0xE5;         /* exactly -0xE5: bge, then <= max */

    r = CAL_$VERIFY(&max_delta, NULL, &interactive, &status);

    ASSERT_EQ((uint8_t)r, 0xFF);
    ASSERT_EQ(write_calls, 0);
}

TEST(read_failure_crashes)
{
    int max_delta = 50;
    char interactive = 0;
    status_$t status = 0;

    reset();
    read_tz_status_value = 0x00080005;

    (void)CAL_$VERIFY(&max_delta, NULL, &interactive, &status);

    ASSERT_EQ(crash_calls, 1);
    ASSERT_EQ(crash_status, 0x00080005);
}

TEST(behind_non_interactive_is_false_without_status)
{
    int max_delta = 50;
    char interactive = 0;
    status_$t status = 0x5555;
    char r;

    reset();
    TIME_$CLOCKH = 1000 - 0xE6;

    r = CAL_$VERIFY(&max_delta, NULL, &interactive, &status);

    ASSERT_EQ(r, 0);
    ASSERT_EQ(status, status_$ok);       /* cleared at entry, never set */
    ASSERT_EQ(write_calls, 1);
    ASSERT_TRUE(write_fmt[0] == cal_$s_minute_slow);
    ASSERT_TRUE(write_arg[0] == &cal_$c_no_args);
    ASSERT_EQ(read_calls, 0);
}

TEST(ahead_message_carries_msg_arg)
{
    int max_delta = 50;
    int days = 3;
    char interactive = 0;
    status_$t status = 0;

    reset();
    TIME_$CLOCKH = 1051;

    (void)CAL_$VERIFY(&max_delta, &days, &interactive, &status);

    ASSERT_EQ(write_calls, 1);
    ASSERT_TRUE(write_fmt[0] == cal_$s_days_elapsed);
    ASSERT_TRUE(write_arg[0] == &days);
}

TEST(interactive_yes_after_junk)
{
    int max_delta = 50;
    char interactive = (char)0xFF;
    status_$t status = 0;
    char r;

    reset();
    TIME_$CLOCKH = 2000;
    answers = "?xy";

    r = CAL_$VERIFY(&max_delta, NULL, &interactive, &status);

    ASSERT_EQ((uint8_t)r, 0xFF);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(read_calls, 3);
    ASSERT_EQ(read_line, 1);
    ASSERT_EQ(read_limit, 6);
    /* message, then three prompts */
    ASSERT_EQ(write_calls, 4);
    ASSERT_TRUE(write_fmt[1] == cal_$s_prompt);
    ASSERT_TRUE(write_fmt[3] == cal_$s_prompt);
}

TEST(interactive_no_refuses)
{
    int max_delta = 50;
    char interactive = (char)0xFF;
    status_$t status = 0;
    char r;

    reset();
    TIME_$CLOCKH = 2000;
    answers = "N";

    r = CAL_$VERIFY(&max_delta, NULL, &interactive, &status);

    ASSERT_EQ(r, 0);
    ASSERT_EQ(status, status_$cal_refused);
    ASSERT_EQ(status, 0x150007);
    ASSERT_EQ(read_calls, 1);
    ASSERT_EQ(write_calls, 3);
    ASSERT_TRUE(write_fmt[2] == cal_$s_set_calendar);
}

TEST(constant_cells_match_image_bytes)
{
    reset();
    ASSERT_EQ(cal_$s_minute_slow[0], '%');
    ASSERT_EQ(cal_$s_minute_slow[1], '/');
    ASSERT_EQ(cal_$s_minute_slow[0x2C], '.');
    ASSERT_EQ(cal_$s_prompt[0x39], '$');
    ASSERT_EQ(cal_$s_prompt[0x3B], 'H');
    ASSERT_EQ(cal_$s_days_elapsed[0x0D], 'a');
    ASSERT_EQ(cal_$c_read_line, 1);
    ASSERT_EQ(cal_$c_read_limit, 6);
    ASSERT_EQ(cal_$c_no_args, 0);
    ASSERT_EQ(cal_$c_days_arg, 3);
}

int main(void)
{
    printf("test_verify:\n");

    RUN_TEST(in_range_is_true_and_clears_drift);
    RUN_TEST(slightly_behind_is_still_true);
    RUN_TEST(read_failure_crashes);
    RUN_TEST(behind_non_interactive_is_false_without_status);
    RUN_TEST(ahead_message_carries_msg_arg);
    RUN_TEST(interactive_yes_after_junk);
    RUN_TEST(interactive_no_refuses);
    RUN_TEST(constant_cells_match_image_bytes);

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
