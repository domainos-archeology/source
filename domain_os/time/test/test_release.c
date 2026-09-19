/*
 * time/test/test_release.c - Unit tests for TIME_$RELEASE (0x00E58B58),
 * TIME_$SET_ITIMER_REAL_CALLBACK (0x00E58A38), TIME_$SET_ITIMER_VIRT_CALLBACK
 * (0x00E58A98) and TIME_$RTE_INT (0x00E163A6)
 *
 * The real .c files are #included.  TIME_$ITIMER_DB is a host arena reached
 * through ARCH_HOST_VA_BASE exactly as time/test/test_itimer.c does it, so
 * the [which][as_id] addressing and the expiry-vs-interval distinction
 * (bead source-e4a2) are exercised against the real code.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "time/time_internal.h"

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

time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];
time_queue_t TIME_$RTEQ;
uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
uid_t PROC2_$UID[PROC2_UID_TABLE_SIZE];
uint8_t IN_RT_INT;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int remove_calls;
static time_queue_t *remove_queue[2];
static time_queue_elem_t *remove_elem[2];

void TIME_$Q_REMOVE_ELEM(time_queue_t *queue, time_queue_elem_t *elem,
                         status_$t *status)
{
    if (remove_calls < 2) {
        remove_queue[remove_calls] = queue;
        remove_elem[remove_calls] = elem;
    }
    remove_calls++;
    *status = status_$time_queue_element_not_found;   /* ignored by RELEASE */
}

static int signal_calls;
static uid_t *signal_uid;
static int16_t signal_number;
static uint32_t signal_param;

void PROC2_$SIGNAL_OS(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                      status_$t *status_ret)
{
    signal_calls++;
    signal_uid = proc_uid;
    signal_number = *signal;
    signal_param = *param;
    *status_ret = status_$ok;
}

static clock_t mock_abs_clock;
static int abs_clock_calls;
void TIME_$ABS_CLOCK(clock_t *clock)
{
    abs_clock_calls++;
    *clock = mock_abs_clock;
}

static int scan_calls;
static time_queue_t *scan_queue;
static clock_t scan_now;
static int in_rt_int_at_scan;
void TIME_$Q_SCAN_QUEUE(time_queue_t *queue, clock_t *now, status_$t *status)
{
    scan_calls++;
    scan_queue = queue;
    scan_now = *now;
    in_rt_int_at_scan = IN_RT_INT;
    *status = status_$ok;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../release.c"
#include "../set_itimer_real_callback.c"
#include "../set_itimer_virt_callback.c"
#include "../rte_int.c"

/* Two halves of 58 entries, addressed as [which][as_id]. */
static uint8_t itimer_arena[2 * ITIMER_DB_WHICH_STRIDE];

static time_queue_elem_t *entry_for(uint16_t which, uint16_t as_id)
{
    return (time_queue_elem_t *)(itimer_arena +
                                 which * ITIMER_DB_WHICH_STRIDE +
                                 as_id * ITIMER_DB_ENTRY_SIZE);
}

static void fill(time_queue_elem_t *e, uint32_t seed)
{
    e->next = seed;
    e->callback = seed + 1;
    e->callback_arg = seed + 2;
    e->expire_high = seed + 3;
    e->expire_low = (uint16_t)(seed + 4);
    e->flags = (uint16_t)(seed + 5);
    e->interval_high = seed + 6;
    e->interval_low = (uint16_t)(seed + 7);
}

static void reset(void)
{
    memset(itimer_arena, 0, sizeof(itimer_arena));
    memset(TIME_$VTQ, 0, sizeof(TIME_$VTQ));
    memset(&TIME_$RTEQ, 0, sizeof(TIME_$RTEQ));
    ARCH_HOST_VA_BASE = (uintptr_t)itimer_arena - ITIMER_DB_BASE;
    PROC1_$CURRENT = 3;
    PROC1_$AS_ID = 5;
    remove_calls = signal_calls = abs_clock_calls = scan_calls = 0;
    IN_RT_INT = 0;
}

/* ==========================================================================
 * TIME_$RELEASE
 * ========================================================================== */

TEST(release_removes_both_and_clears_expiry_only)
{
    time_queue_elem_t *real = entry_for(0, 5);
    time_queue_elem_t *virt = entry_for(1, 5);
    time_queue_elem_t *other = entry_for(0, 6);

    reset();
    fill(real, 0x100);
    fill(virt, 0x200);
    fill(other, 0x300);

    TIME_$RELEASE();

    ASSERT_EQ(2, remove_calls);
    ASSERT_PTR_EQ(&TIME_$RTEQ, remove_queue[0]);
    ASSERT_PTR_EQ(real, remove_elem[0]);
    ASSERT_PTR_EQ(&TIME_$VTQ[2], remove_queue[1]);     /* PROC1_$CURRENT 3 */
    ASSERT_PTR_EQ(virt, remove_elem[1]);

    /* expiry cleared ... */
    ASSERT_EQ(0, real->expire_high);
    ASSERT_EQ(0, real->expire_low);
    ASSERT_EQ(0, virt->expire_high);
    ASSERT_EQ(0, virt->expire_low);
    /* ... interval and everything else untouched */
    ASSERT_EQ(0x106, real->interval_high);
    ASSERT_EQ(0x107, real->interval_low);
    ASSERT_EQ(0x105, real->flags);
    ASSERT_EQ(0x206, virt->interval_high);
    ASSERT_EQ(0x207, virt->interval_low);
    /* a neighbouring entry is not touched */
    ASSERT_EQ(0x303, other->expire_high);
}

/* ==========================================================================
 * The two itimer callbacks
 * ========================================================================== */

/* The deferred-path argument: the address of a cell holding the address of
 * the 4 bytes DXM copied out of elem->callback_arg (the as_id longword). */
static uint32_t data_word;
static uint32_t *data_ptr;

TEST(real_callback_signals_when_expiry_set)
{
    time_queue_elem_t *real = entry_for(0, 7);

    reset();
    data_word = 7;
    data_ptr = &data_word;
    real->expire_high = 0;
    real->expire_low = 1;
    real->interval_high = 0;

    TIME_$SET_ITIMER_REAL_CALLBACK((time_$callback_arg_t)&data_ptr);

    ASSERT_EQ(1, signal_calls);
    ASSERT_PTR_EQ(&PROC2_$UID[7], signal_uid);
    ASSERT_EQ(0x000E, signal_number);
    ASSERT_EQ(0x000D0007, signal_param);
}

TEST(real_callback_silent_when_expiry_zero_even_if_interval_set)
{
    time_queue_elem_t *real = entry_for(0, 7);

    reset();
    data_word = 7;
    data_ptr = &data_word;
    real->interval_high = 0x1234;
    real->interval_low = 0x5678;

    TIME_$SET_ITIMER_REAL_CALLBACK((time_$callback_arg_t)&data_ptr);

    ASSERT_EQ(0, signal_calls);
}

TEST(virt_callback_uses_virtual_half)
{
    time_queue_elem_t *real = entry_for(0, 9);
    time_queue_elem_t *virt = entry_for(1, 9);

    reset();
    data_word = 0xABCD0009;         /* only the low word is the as_id */
    data_ptr = &data_word;
    real->expire_high = 0x77;       /* must not be consulted */

    TIME_$SET_ITIMER_VIRT_CALLBACK((time_$callback_arg_t)&data_ptr);
    ASSERT_EQ(0, signal_calls);

    virt->expire_high = 0x10;
    TIME_$SET_ITIMER_VIRT_CALLBACK((time_$callback_arg_t)&data_ptr);
    ASSERT_EQ(1, signal_calls);
    ASSERT_PTR_EQ(&PROC2_$UID[9], signal_uid);
    ASSERT_EQ(0x001D, signal_number);
    ASSERT_EQ(0x000D0008, signal_param);
}

/* ==========================================================================
 * TIME_$RTE_INT
 * ========================================================================== */

TEST(rte_int_scans_rteq_then_clears_flag)
{
    reset();
    IN_RT_INT = 0xFF;
    mock_abs_clock = (clock_t){ 0x1234, 0x5678 };

    TIME_$RTE_INT();

    ASSERT_EQ(1, abs_clock_calls);
    ASSERT_EQ(1, scan_calls);
    ASSERT_PTR_EQ(&TIME_$RTEQ, scan_queue);
    ASSERT_EQ(0x1234, scan_now.high);
    ASSERT_EQ(0x5678, scan_now.low);
    ASSERT_EQ(0xFF, in_rt_int_at_scan);    /* cleared only after the scan */
    ASSERT_EQ(0, IN_RT_INT);
}

int main(void)
{
    printf("test_release:\n");

    RUN_TEST(release_removes_both_and_clears_expiry_only);
    RUN_TEST(real_callback_signals_when_expiry_set);
    RUN_TEST(real_callback_silent_when_expiry_zero_even_if_interval_set);
    RUN_TEST(virt_callback_uses_virtual_half);
    RUN_TEST(rte_int_scans_rteq_then_clears_flag);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
