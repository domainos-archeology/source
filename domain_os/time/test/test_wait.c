/*
 * time/test/test_wait.c - Unit tests for TIME_$WAIT (0x00E1650A) and
 * TIME_$WAIT2 (0x00E16654)
 *
 * The real time/wait.c and time/wait2.c are #included; TIME_$ADVANCE,
 * EC_$WAIT, TIME_$CANCEL, CRASH_SYSTEM, the clock readers and the 48-bit
 * arithmetic are mocked.  The EC_$WAIT mock records both by-value arrays
 * and can set the element's flags word to drive the crash paths.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "time/time_internal.h"
#include "misc/crash_system.h"

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

#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((uintptr_t)(e), (uintptr_t)(a))

uint16_t PROC1_$AS_ID;
#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);

static int ec_init_calls;
void EC_$INIT(ec_$eventcount_t *ec) { ec_init_calls++; memset(ec, 0, sizeof(*ec)); }

static clock_t mock_clock = { 100, 0x10 };
static clock_t mock_abs = { 500, 0x40 };
void TIME_$CLOCK(clock_t *c) { *c = mock_clock; }
void TIME_$ABS_CLOCK(clock_t *c) { *c = mock_abs; }

int8_t SUB48(clock_t *dst, clock_t *src)
{
    uint16_t dl = dst->low, sl = src->low;
    dst->low = (uint16_t)(dl - sl);
    dst->high = dst->high - src->high - (dl < sl ? 1u : 0u);
    return ((int32_t)dst->high >= 0) ? -1 : 0;
}

void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t low = (uint32_t)dst->low + (uint32_t)src->low;
    dst->low = (uint16_t)low;
    dst->high = dst->high + src->high + (low >> 16);
}

static int advance_calls;
static uint16_t advance_is_absolute;
static clock_t advance_when;
static ec_$eventcount_t *advance_ec;
static time_queue_elem_t *advance_elem;
static status_$t advance_status_value;
void TIME_$ADVANCE(uint16_t *is_absolute, clock_t *when, ec_$eventcount_t *ec,
                   time_queue_elem_t *elem, status_$t *status)
{
    advance_calls++;
    advance_is_absolute = *is_absolute;
    advance_when = *when;
    advance_ec = ec;
    advance_elem = elem;
    *status = advance_status_value;
}

static int wait_calls;
static ec_$wait_ecs_t wait_ecs;
static ec_$wait_vals_t wait_vals;
static int16_t wait_result;
static uint16_t flags_after_wait;
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    wait_calls++;
    wait_ecs = ecs;
    wait_vals = vals;
    advance_elem->flags = flags_after_wait;
    return wait_result;
}

static int cancel_calls;
static int32_t cancel_value;
static time_queue_elem_t *cancel_elem;
void TIME_$CANCEL(int32_t wait_value, time_queue_elem_t *elem, status_$t *status)
{
    cancel_calls++;
    cancel_value = wait_value;
    cancel_elem = elem;
    *status = status_$ok;
}

static int crash_calls;
static const status_$t *crash_cell;
void CRASH_SYSTEM(const status_$t *status_p) { crash_calls++; crash_cell = status_p; }

#include "../wait.c"
#include "../wait2.c"

static void reset(void)
{
    PROC1_$AS_ID = 3;
    memset(FIM_$WIRED_DATA.quit_value, 0, sizeof(FIM_$WIRED_DATA.quit_value));
    memset(FIM_$WIRED_DATA.quit_ec, 0, sizeof(FIM_$WIRED_DATA.quit_ec));
    FIM_$WIRED_DATA.quit_value[3] = 40;
    FIM_$WIRED_DATA.quit_ec[3].value = 41;
    ec_init_calls = advance_calls = wait_calls = cancel_calls = crash_calls = 0;
    advance_status_value = status_$ok;
    wait_result = 0;
    flags_after_wait = 0;
    crash_cell = NULL;
}

/* ---------------------------------------------------------------- WAIT */

TEST(wait_relative_timer_fires)
{
    uint16_t dtype = 0;
    clock_t delay = { 0, 250 };
    status_$t status = 0x5555;

    reset();
    TIME_$WAIT(&dtype, &delay, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, ec_init_calls);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ(0, advance_is_absolute);
    ASSERT_EQ(0, advance_when.high);
    ASSERT_EQ(250, advance_when.low);
    ASSERT_EQ(1, wait_calls);
    ASSERT_PTR_EQ(advance_ec, wait_ecs.ec[0]);
    ASSERT_PTR_EQ(&FIM_$WIRED_DATA.quit_ec[3], wait_ecs.ec[1]);
    ASSERT_PTR_EQ(NULL, wait_ecs.ec[2]);
    ASSERT_EQ(1, wait_vals.val[0]);
    ASSERT_EQ(41, wait_vals.val[1]);                /* FIM_$QUIT_VALUE + 1 */
    ASSERT_EQ(0, wait_vals.val[2]);
    ASSERT_EQ(0, cancel_calls);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(40, FIM_$WIRED_DATA.quit_value[3]);              /* untouched */
}

/* delay_type 1: the value is re-based from the raw clock onto the absolute one. */
TEST(wait_type1_rebases_delay)
{
    uint16_t dtype = 1;
    clock_t delay = { 100, 0x30 };
    status_$t status = 0;

    reset();
    TIME_$WAIT(&dtype, &delay, &status);

    /* delay - clock(100:10) + abs(500:40) = 500:60 */
    ASSERT_EQ(1, advance_is_absolute);
    ASSERT_EQ(500, advance_when.high);
    ASSERT_EQ(0x60, advance_when.low);
}

TEST(wait_advance_failure_returns_without_flag_check)
{
    uint16_t dtype = 0;
    clock_t delay = { 0, 1 };
    status_$t status = 0;

    reset();
    advance_status_value = 0x000D000E;
    TIME_$WAIT(&dtype, &delay, &status);

    ASSERT_EQ(0x000D000E, status);
    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(0, crash_calls);
}

TEST(wait_quit_cancels_and_latches)
{
    uint16_t dtype = 0;
    clock_t delay = { 0, 1 };
    status_$t status = 0;

    reset();
    wait_result = 1;                                /* the quit eventcount */
    TIME_$WAIT(&dtype, &delay, &status);

    ASSERT_EQ(1, cancel_calls);
    ASSERT_EQ(1, cancel_value);
    ASSERT_PTR_EQ(advance_elem, cancel_elem);
    ASSERT_EQ(status_$time_quit_while_waiting, status);
    ASSERT_EQ(0x000D0003, status);
    ASSERT_EQ(41, FIM_$WIRED_DATA.quit_value[3]);              /* = FIM_$WIRED_DATA.quit_ec[3].value */
    ASSERT_EQ(0, crash_calls);
}

TEST(wait_crashes_when_element_still_flagged)
{
    uint16_t dtype = 0;
    clock_t delay = { 0, 1 };
    status_$t status = 0;

    reset();
    flags_after_wait = TIME_QELEM_IN_QUEUE;
    TIME_$WAIT(&dtype, &delay, &status);

    ASSERT_EQ(1, crash_calls);
    ASSERT_PTR_EQ(&time_$c_queue_elem_in_use_crash, crash_cell);
    ASSERT_EQ(0x000D000D, *crash_cell);
}

/* --------------------------------------------------------------- WAIT2 */

TEST(wait2_timer_expires_returns_false)
{
    uint16_t dtype = 0;
    clock_t delay = { 0, 7 };
    ec_$eventcount_t extra;
    uint32_t count = 99;
    status_$t status = 0x5555;
    int8_t r;

    reset();
    wait_result = 1;                                /* the timer's ec */
    r = TIME_$WAIT2(&dtype, &delay, &extra, &count, &status);

    ASSERT_EQ(0, r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, advance_is_absolute);
    ASSERT_EQ(7, advance_when.low);
    ASSERT_PTR_EQ(&extra, wait_ecs.ec[0]);
    ASSERT_PTR_EQ(advance_ec, wait_ecs.ec[1]);
    ASSERT_PTR_EQ(NULL, wait_ecs.ec[2]);
    ASSERT_EQ(99, wait_vals.val[0]);
    ASSERT_EQ(1, wait_vals.val[1]);
    ASSERT_EQ(0, cancel_calls);
}

TEST(wait2_extra_ec_fires_returns_true_and_cancels)
{
    uint16_t dtype = 1;
    clock_t delay = { 0, 7 };
    ec_$eventcount_t extra;
    uint32_t count = 5;
    status_$t status = 0;
    int8_t r;

    reset();
    wait_result = 0;
    r = TIME_$WAIT2(&dtype, &delay, &extra, &count, &status);

    ASSERT_EQ(-1, r);
    ASSERT_EQ(1, advance_is_absolute);
    ASSERT_EQ(1, cancel_calls);
    ASSERT_EQ(1, cancel_value);
    ASSERT_PTR_EQ(advance_elem, cancel_elem);
    ASSERT_EQ(status_$ok, status);
}

TEST(wait2_advance_failure_crashes)
{
    uint16_t dtype = 0;
    clock_t delay = { 0, 7 };
    ec_$eventcount_t extra;
    uint32_t count = 5;
    status_$t status = 0;

    reset();
    advance_status_value = 0x000D000D;
    (void)TIME_$WAIT2(&dtype, &delay, &extra, &count, &status);

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x000D000D, *crash_cell);
    ASSERT_EQ(0x000D000D, status);
    ASSERT_EQ(0, wait_calls);
}

TEST(wait2_shares_the_wait_crash_cell)
{
    uint16_t dtype = 0;
    clock_t delay = { 0, 7 };
    ec_$eventcount_t extra;
    uint32_t count = 5;
    status_$t status = 0;

    reset();
    wait_result = 1;
    flags_after_wait = TIME_QELEM_IN_QUEUE;
    (void)TIME_$WAIT2(&dtype, &delay, &extra, &count, &status);

    ASSERT_EQ(1, crash_calls);
    ASSERT_PTR_EQ(&time_$c_queue_elem_in_use_crash, crash_cell);
}

int main(void)
{
    printf("test_wait:\n");
    RUN_TEST(wait_relative_timer_fires);
    RUN_TEST(wait_type1_rebases_delay);
    RUN_TEST(wait_advance_failure_returns_without_flag_check);
    RUN_TEST(wait_quit_cancels_and_latches);
    RUN_TEST(wait_crashes_when_element_still_flagged);
    RUN_TEST(wait2_timer_expires_returns_false);
    RUN_TEST(wait2_extra_ec_fires_returns_true_and_cancels);
    RUN_TEST(wait2_advance_failure_crashes);
    RUN_TEST(wait2_shares_the_wait_crash_cell);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
