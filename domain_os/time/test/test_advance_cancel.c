/*
 * time/test/test_advance_cancel.c - Unit tests for TIME_$ADVANCE
 * (0x00E16454), TIME_$ADVANCE_CALLBACK (0x00E16434) and TIME_$CANCEL
 * (0x00E164A4)
 *
 * The three real .c files are #included; the queue routines, EC_$WAIT,
 * EC_$ADVANCE_WITHOUT_DISPATCH and CRASH_SYSTEM are mocked and record what
 * they were handed.  Element callback_arg fields are 32-bit target virtual
 * addresses, so ARCH_HOST_VA_BASE is pointed at a local arena and the
 * eventcounts live inside it.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "time/time_internal.h"
#include "misc/crash_system.h"

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
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

#define ASSERT_PTR_EQ(expected, actual)                                       \
    do {                                                                      \
        const void *_e = (const void *)(expected);                            \
        const void *_a = (const void *)(actual);                              \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: %p, Got: %p at line %d\n",          \
                   _e, _a, __LINE__);                                         \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

time_queue_t TIME_$RTEQ;
clock_t time_$zero_interval = { 0, 0 };

/* Arena standing in for target memory; eventcounts are placed inside it. */
static uint8_t arena[0x1000];
#define EC_VA        0x100
#define OTHER_EC_VA  0x200

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static clock_t mock_now;
static int abs_clock_calls;

void TIME_$ABS_CLOCK(clock_t *clock)
{
    abs_clock_calls++;
    *clock = mock_now;
}

static int add_calls;
static time_queue_t *add_queue;
static clock_t *add_when;
static uint16_t add_is_absolute;
static clock_t add_now;
static void *add_callback;
static void *add_callback_arg;
static uint16_t add_flags;
static clock_t *add_interval;
static time_queue_elem_t *add_qelem;
static status_$t add_status_value;

void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, clock_t *when,
                          uint16_t is_absolute, clock_t *now,
                          void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *qelem, status_$t *status)
{
    add_calls++;
    add_queue = queue;
    add_when = when;
    add_is_absolute = is_absolute;
    add_now = *now;
    add_callback = callback;
    add_callback_arg = callback_arg;
    add_flags = flags;
    add_interval = interval;
    add_qelem = qelem;
    *status = add_status_value;
}

static int remove_calls;
static time_queue_t *remove_queue;
static time_queue_elem_t *remove_elem;
static status_$t remove_status_value;

void TIME_$Q_REMOVE_ELEM(time_queue_t *queue, time_queue_elem_t *elem,
                         status_$t *status)
{
    remove_calls++;
    remove_queue = queue;
    remove_elem = elem;
    *status = remove_status_value;
}

static int wait_calls;
static ec_$wait_ecs_t wait_ecs;
static ec_$wait_vals_t wait_vals;

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    wait_calls++;
    wait_ecs = ecs;
    wait_vals = vals;
    return 0;
}

static int advance_calls;
static ec_$eventcount_t *advance_ec;

void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec)
{
    advance_calls++;
    advance_ec = ec;
}

static int crash_calls;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
}

static void reset(void)
{
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(&TIME_$RTEQ, 0, sizeof(TIME_$RTEQ));
    mock_now.high = 0x1234;
    mock_now.low = 0x5678;
    abs_clock_calls = 0;
    add_calls = 0;
    add_status_value = status_$ok;
    remove_calls = 0;
    remove_status_value = status_$ok;
    wait_calls = 0;
    memset(&wait_ecs, 0, sizeof(wait_ecs));
    memset(&wait_vals, 0, sizeof(wait_vals));
    advance_calls = 0;
    advance_ec = NULL;
    crash_calls = 0;
    crash_status = 0;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../advance.c"
#include "../advance_callback.c"
#include "../cancel.c"

/* ==========================================================================
 * TIME_$ADVANCE
 * ========================================================================== */

TEST(advance_enters_rteq_with_shared_cells)
{
    uint16_t is_absolute = 1;
    clock_t when = { 0xAA, 0xBB };
    time_queue_elem_t elem;
    ec_$eventcount_t *ec = (ec_$eventcount_t *)ARCH_VA_TO_PTR(EC_VA);
    status_$t status = 0x77777777;

    reset();
    add_status_value = 0x000D000E;
    TIME_$ADVANCE(&is_absolute, &when, ec, &elem, &status);

    ASSERT_EQ(1, abs_clock_calls);
    ASSERT_EQ(1, add_calls);
    ASSERT_PTR_EQ(&TIME_$RTEQ, add_queue);           /* (0x1608,A5) */
    ASSERT_PTR_EQ(&when, add_when);
    ASSERT_EQ(1, add_is_absolute);
    ASSERT_EQ(0x1234, add_now.high);
    ASSERT_EQ(0x5678, add_now.low);
    ASSERT_PTR_EQ((void *)TIME_$ADVANCE_CALLBACK, add_callback);
    ASSERT_PTR_EQ(ec, add_callback_arg);
    ASSERT_EQ(0, add_flags);
    ASSERT_PTR_EQ(&time_$zero_interval, add_interval); /* (0x1614,A5) */
    ASSERT_PTR_EQ(&elem, add_qelem);
    /* the status is whatever TIME_$Q_ADD_CALLBACK left there */
    ASSERT_EQ(0x000D000E, status);
}

TEST(advance_passes_relative_flag_by_value)
{
    uint16_t is_absolute = 0;
    clock_t when = { 1, 2 };
    time_queue_elem_t elem;
    status_$t status = 0;

    reset();
    TIME_$ADVANCE(&is_absolute, &when, NULL, &elem, &status);

    ASSERT_EQ(0, add_is_absolute);
    ASSERT_PTR_EQ(NULL, add_callback_arg);
    ASSERT_EQ(status_$ok, status);
}

/* ==========================================================================
 * TIME_$ADVANCE_CALLBACK
 * ========================================================================== */

TEST(advance_callback_advances_elements_ec)
{
    time_queue_elem_t elem;
    time_queue_elem_t *elem_ptr = &elem;

    reset();
    memset(&elem, 0, sizeof(elem));
    elem.callback_arg = EC_VA;

    /* the direct-path argument: a cell holding the element's address */
    TIME_$ADVANCE_CALLBACK(&elem_ptr);

    ASSERT_EQ(1, advance_calls);
    ASSERT_PTR_EQ(ARCH_VA_TO_PTR(EC_VA), advance_ec);
}

/* ==========================================================================
 * TIME_$CANCEL
 * ========================================================================== */

TEST(cancel_removes_from_rteq)
{
    time_queue_elem_t elem;
    status_$t status = 0x77777777;

    reset();
    memset(&elem, 0, sizeof(elem));
    elem.callback_arg = EC_VA;

    TIME_$CANCEL(1, &elem, &status);

    ASSERT_EQ(1, remove_calls);
    ASSERT_PTR_EQ(&TIME_$RTEQ, remove_queue);
    ASSERT_PTR_EQ(&elem, remove_elem);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(0, crash_calls);
}

/* status_$time_queue_element_not_in_use: wait on the element's eventcount
 * for the by-value wait_value, then report success. */
TEST(cancel_waits_when_element_not_in_use)
{
    time_queue_elem_t elem;
    status_$t status = 0;

    reset();
    memset(&elem, 0, sizeof(elem));
    elem.callback_arg = OTHER_EC_VA;
    remove_status_value = status_$time_queue_element_not_in_use;

    TIME_$CANCEL(0x00010002, &elem, &status);

    ASSERT_EQ(1, wait_calls);
    ASSERT_PTR_EQ(ARCH_VA_TO_PTR(OTHER_EC_VA), wait_ecs.ec[0]);
    ASSERT_PTR_EQ(NULL, wait_ecs.ec[1]);
    ASSERT_PTR_EQ(NULL, wait_ecs.ec[2]);
    ASSERT_EQ(0x00010002, wait_vals.val[0]);
    ASSERT_EQ(0, wait_vals.val[1]);
    ASSERT_EQ(0, wait_vals.val[2]);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, crash_calls);
}

/* Any other failure crashes with the status still in place. */
TEST(cancel_crashes_on_other_status)
{
    time_queue_elem_t elem;
    status_$t status = 0;

    reset();
    memset(&elem, 0, sizeof(elem));
    remove_status_value = status_$time_queue_element_not_found;

    TIME_$CANCEL(1, &elem, &status);

    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$time_queue_element_not_found, crash_status);
    ASSERT_EQ(status_$time_queue_element_not_found, status);
}

int main(void)
{
    printf("test_advance_cancel:\n");

    RUN_TEST(advance_enters_rteq_with_shared_cells);
    RUN_TEST(advance_passes_relative_flag_by_value);
    RUN_TEST(advance_callback_advances_elements_ec);
    RUN_TEST(cancel_removes_from_rteq);
    RUN_TEST(cancel_waits_when_element_not_in_use);
    RUN_TEST(cancel_crashes_on_other_status);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
