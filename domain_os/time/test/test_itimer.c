/*
 * time/test/test_itimer.c - Unit tests for the interval-timer entry points.
 *
 * Compiles the real time/clock_to_itimer.c, time/itimer_to_clock.c,
 * time/get_itimer.c, time/set_itimer.c and time/set_cpu_limit.c and supplies
 * the globals and callees they reach, so the argument DIRECTION of
 * time_$clock_to_itimer / time_$itimer_to_clock (bead source-6r3m) is
 * exercised through the real call sites rather than restated.
 *
 * time/set_cpu_limit.c dereferences TIME_$CPU_LIMIT_DB at its target virtual
 * address, so the test points ARCH_HOST_VA_BASE at a local arena.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#include "time/time_internal.h"

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];
time_queue_t TIME_$RTEQ;

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
uid_t PROC2_$UID[PROC2_UID_TABLE_SIZE];

/* The CPU-limit constant cells live in time/set_cpu_limit_callback.c. */
const int16_t time_$c_cpu_limit_signal = 0x001B;
const status_$t time_$c_cpu_limit_fault = 0x000D000B;

/* The arena that stands in for TIME_$CPU_LIMIT_DB. */
static uint8_t cpu_limit_arena[PROC2_UID_TABLE_SIZE * CPU_LIMIT_DB_ENTRY_SIZE];

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static clock_t mock_cput;

void PROC1_$GET_CPUT8(void *time_ret)
{
    *(clock_t *)time_ret = mock_cput;
}

/* SUB48 is the real 48-bit subtract from cal/; reproduce it here so the test
 * program stays one translation unit. */
int8_t SUB48(clock_t *dst, clock_t *src)
{
    uint16_t dst_low = dst->low;
    uint16_t src_low = src->low;

    dst->low = (uint16_t)(dst_low - src_low);
    dst->high = dst->high - src->high - (dst_low < src_low ? 1u : 0u);
    return ((int32_t)dst->high >= 0) ? -1 : 0;
}

void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t low = (uint32_t)dst->low + (uint32_t)src->low;
    dst->low = (uint16_t)low;
    dst->high = dst->high + src->high + (low >> 16);
}

static int remove_calls;
static time_queue_t *remove_queue;
static time_queue_elem_t *remove_elem;

void TIME_$Q_REMOVE_ELEM(time_queue_t *queue, time_queue_elem_t *elem,
                         status_$t *status)
{
    remove_calls++;
    remove_queue = queue;
    remove_elem = elem;
    *status = status_$ok;
}

static int add_calls;
static time_queue_t *add_queue;
static clock_t add_when;
static uint16_t add_is_absolute;
static clock_t add_now;
static void *add_callback;
static void *add_callback_arg;
static uint16_t add_flags;
static clock_t add_interval;
static time_queue_elem_t *add_qelem;

void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, clock_t *when,
                          uint16_t is_absolute, clock_t *now,
                          void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *qelem, status_$t *status)
{
    add_calls++;
    add_queue = queue;
    add_when = *when;
    add_is_absolute = is_absolute;
    add_now = *now;
    add_callback = callback;
    add_callback_arg = callback_arg;
    add_flags = flags;
    add_interval = *interval;
    add_qelem = qelem;
    *status = status_$ok;
}

void TIME_$Q_ENTER_ELEM(time_queue_t *queue, clock_t *when,
                        time_queue_elem_t *elem, status_$t *status)
{
    (void)queue; (void)when; (void)elem;
    *status = status_$ok;
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

void PROC2_$SET_CLEANUP(uint16_t bit_num) { (void)bit_num; }

void TIME_$SET_CPU_LIMIT_CALLBACK(time_$callback_arg_t arg) { (void)arg; }

void TIME_$SET_ITIMER_REAL_CALLBACK(time_$callback_arg_t arg) { (void)arg; }
void TIME_$SET_ITIMER_VIRT_CALLBACK(time_$callback_arg_t arg) { (void)arg; }

static clock_t mock_abs_clock;

void TIME_$ABS_CLOCK(clock_t *clock) { *clock = mock_abs_clock; }

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../clock_to_itimer.c"
#include "../itimer_to_clock.c"
#include "../get_itimer_internal.c"
#include "../set_itimer_internal.c"
#include "../get_itimer.c"
#include "../set_itimer.c"
#include "../set_cpu_limit.c"

/* ==========================================================================
 * The arena that stands in for TIME_$ITIMER_DB, and the recorders the
 * wrapper tests use in place of the old hand-written mocks.
 * ========================================================================== */

/* Two halves of 58 entries, addressed as [which][as_id]. */
static uint8_t itimer_arena[2 * ITIMER_DB_WHICH_STRIDE];

static time_queue_elem_t *itimer_entry_for(uint16_t which, uint16_t as_id)
{
    return (time_queue_elem_t *)(itimer_arena +
                                 which * ITIMER_DB_WHICH_STRIDE +
                                 as_id * ITIMER_DB_ENTRY_SIZE);
}

static void itimer_setup(void)
{
    memset(itimer_arena, 0, sizeof(itimer_arena));
    memset(TIME_$VTQ, 0, sizeof(TIME_$VTQ));
    memset(&TIME_$RTEQ, 0, sizeof(TIME_$RTEQ));
    ARCH_HOST_VA_BASE = (uintptr_t)itimer_arena - ITIMER_DB_BASE;
    PROC1_$CURRENT = 3;
    PROC1_$AS_ID = 5;
    remove_calls = add_calls = signal_calls = 0;
    mock_abs_clock = (clock_t){ 0, 0 };
    mock_cput = (clock_t){ 0, 0 };
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * 0xE58C02: dest is the FIRST argument, source the second, and the carry runs
 * from bit 15 of the source low word into the destination high longword.
 */
TEST(clock_to_itimer_direction_and_carry)
{
    clock_t src = { 0x00001234, 0x8001 };
    clock_t dst = { 0xDEADBEEF, 0xCAFE };

    time_$clock_to_itimer(&dst, &src);

    /* the source must be untouched */
    ASSERT_EQ(0x00001234, src.high);
    ASSERT_EQ(0x8001, src.low);

    /* 0x1234*2 + carry, 0x8001*2 truncated to 16 bits */
    ASSERT_EQ(0x00002469, dst.high);
    ASSERT_EQ(0x0002, dst.low);
}

TEST(clock_to_itimer_no_carry_below_8000)
{
    clock_t src = { 0x00000001, 0x7FFF };
    clock_t dst = { 0, 0 };

    time_$clock_to_itimer(&dst, &src);

    ASSERT_EQ(0x00000002, dst.high);   /* bcs taken: no addq */
    ASSERT_EQ(0xFFFE, dst.low);
}

TEST(clock_to_itimer_high_is_logical)
{
    clock_t src = { 0xFFFFFFFF, 0x0000 };
    clock_t dst = { 0, 0 };

    time_$clock_to_itimer(&dst, &src);

    ASSERT_EQ(0xFFFFFFFEu, dst.high);  /* add.l wraps, no saturation */
    ASSERT_EQ(0x0000, dst.low);
}

/* 0xE58C3A: bit 0 of the source high longword becomes bit 15 of the low word */
TEST(itimer_to_clock_direction_and_borrow)
{
    clock_t src = { 0x00002469, 0x0002 };
    clock_t dst = { 0xDEADBEEF, 0xCAFE };

    time_$itimer_to_clock(&dst, &src);

    ASSERT_EQ(0x00002469, src.high);
    ASSERT_EQ(0x0002, src.low);

    ASSERT_EQ(0x00001234, dst.high);
    ASSERT_EQ(0x8001, dst.low);
}

TEST(itimer_to_clock_shift_is_logical)
{
    clock_t src = { 0x80000000, 0x0000 };
    clock_t dst = { 0, 0 };

    time_$itimer_to_clock(&dst, &src);

    ASSERT_EQ(0x40000000, dst.high);   /* lsr.l, not asr.l */
    ASSERT_EQ(0x0000, dst.low);
}

/* Round trip: doubling then halving is the identity for any 47-bit value. */
TEST(shift_round_trip)
{
    clock_t src = { 0x12345678, 0xABCD };
    clock_t mid = { 0, 0 };
    clock_t back = { 0, 0 };

    time_$clock_to_itimer(&mid, &src);
    time_$itimer_to_clock(&back, &mid);

    ASSERT_EQ(src.high, back.high);
    ASSERT_EQ(src.low, back.low);
}

/* ==========================================================================
 * time_$get_itimer_internal (0xE58C74)
 * ========================================================================== */

/* 0xE58CAC: a queue element whose flags bit 0 is clear reports two zeroes. */
TEST(get_internal_not_in_use_returns_zeros)
{
    clock_t interval = { 0x11111111, 0x2222 };
    clock_t value = { 0x33333333, 0x4444 };

    itimer_setup();
    itimer_entry_for(0, 5)->flags = 0;               /* not in use */
    itimer_entry_for(0, 5)->interval_high = 0x99;
    itimer_entry_for(0, 5)->expire_high = 0x99;

    time_$get_itimer_internal(0, &interval, &value);

    ASSERT_EQ(0, interval.high);
    ASSERT_EQ(0, interval.low);
    ASSERT_EQ(0, value.high);
    ASSERT_EQ(0, value.low);
}

/*
 * The armed case: the interval comes out of +0x14 verbatim and the value is
 * the +0x0C expiry minus the wall clock (0xE58CEA).
 */
TEST(get_internal_real_returns_remaining_time)
{
    clock_t interval = { 0, 0 };
    clock_t value = { 0, 0 };
    time_queue_elem_t *entry;

    itimer_setup();
    entry = itimer_entry_for(0, 5);
    entry->flags = ITIMER_FLAG_IN_USE;
    entry->interval_high = 0x00000003;
    entry->interval_low = 0x0001;
    entry->expire_high = 0x00001000;
    entry->expire_low = 0x0000;
    mock_abs_clock = (clock_t){ 0x00000400, 0x0000 };

    time_$get_itimer_internal(0, &interval, &value);

    ASSERT_EQ(0x00000003, interval.high);
    ASSERT_EQ(0x0001, interval.low);
    ASSERT_EQ(0x00000C00, value.high);
    ASSERT_EQ(0x0000, value.low);
    /* the database itself is not modified */
    ASSERT_EQ(0x00001000, entry->expire_high);
}

/* A deadline already in the past comes back as zero (0xE58CF6). */
TEST(get_internal_expired_value_is_zeroed)
{
    clock_t interval = { 0, 0 };
    clock_t value = { 0, 0 };

    itimer_setup();
    itimer_entry_for(0, 5)->flags = ITIMER_FLAG_IN_USE;
    itimer_entry_for(0, 5)->interval_high = 0x00000007;
    itimer_entry_for(0, 5)->expire_high = 0x00000100;
    mock_abs_clock = (clock_t){ 0x00000400, 0x0000 };

    time_$get_itimer_internal(0, &interval, &value);

    ASSERT_EQ(0x00000007, interval.high);   /* the interval is still reported */
    ASSERT_EQ(0, value.high);
    ASSERT_EQ(0, value.low);
}

/*
 * which != 0 reads the process CPU clock instead (0xE58CD8), and indexes the
 * virtual half of the database, 0x658 bytes on.
 */
TEST(get_internal_virtual_uses_cput_and_second_half)
{
    clock_t interval = { 0, 0 };
    clock_t value = { 0, 0 };

    itimer_setup();
    /* the real half at the same as_id must NOT be the one read */
    itimer_entry_for(0, 5)->flags = ITIMER_FLAG_IN_USE;
    itimer_entry_for(0, 5)->interval_high = 0xDEAD;

    itimer_entry_for(1, 5)->flags = ITIMER_FLAG_IN_USE;
    itimer_entry_for(1, 5)->interval_high = 0x00000005;
    itimer_entry_for(1, 5)->expire_high = 0x00000080;
    mock_cput = (clock_t){ 0x00000020, 0x0000 };

    time_$get_itimer_internal(1, &interval, &value);

    ASSERT_EQ(0x00000005, interval.high);
    ASSERT_EQ(0x00000060, value.high);
}

/* ==========================================================================
 * time_$set_itimer_internal (0xE58D14)
 * ========================================================================== */

/* 0xE58D7E: the top word of either high longword reaching 0x8000 is fatal. */
TEST(set_internal_rejects_too_large_value)
{
    clock_t interval = { 0x00000001, 0x0000 };
    clock_t value = { 0x80000000, 0x0000 };
    clock_t ointerval = { 0, 0 };
    clock_t ovalue = { 0, 0 };
    status_$t status = status_$ok;

    itimer_setup();

    time_$set_itimer_internal(0, &interval, &value, &ointerval, &ovalue,
                              &status);

    ASSERT_EQ(status_$time_relative_time_is_too_large, status);
    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(0, add_calls);
}

TEST(set_internal_rejects_too_large_interval)
{
    clock_t interval = { 0xFFFF0000, 0x0000 };
    clock_t value = { 0x00000001, 0x0000 };
    clock_t ointerval = { 0, 0 };
    clock_t ovalue = { 0, 0 };
    status_$t status = status_$ok;

    itimer_setup();

    time_$set_itimer_internal(0, &interval, &value, &ointerval, &ovalue,
                              &status);

    ASSERT_EQ(status_$time_relative_time_is_too_large, status);
    ASSERT_EQ(0, add_calls);
}

/* 0xE58DD8: a zero it_value dequeues and clears the expiry, nothing is armed. */
TEST(set_internal_zero_value_disarms)
{
    clock_t interval = { 0x00000009, 0x0000 };
    clock_t value = { 0, 0 };
    clock_t ointerval = { 0, 0 };
    clock_t ovalue = { 0, 0 };
    status_$t status = -1;
    time_queue_elem_t *entry;

    itimer_setup();
    entry = itimer_entry_for(0, 5);
    entry->flags = ITIMER_FLAG_IN_USE;
    entry->interval_high = 0x00000002;
    entry->interval_low = 0x0001;
    entry->expire_high = 0x00000500;
    mock_abs_clock = (clock_t){ 0x00000100, 0x0000 };

    time_$set_itimer_internal(0, &interval, &value, &ointerval, &ovalue,
                              &status);

    /* the previous setting was reported first (0xE58D9E) */
    ASSERT_EQ(0x00000002, ointerval.high);
    ASSERT_EQ(0x0001, ointerval.low);
    ASSERT_EQ(0x00000400, ovalue.high);

    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ((uintptr_t)&TIME_$RTEQ, (uintptr_t)remove_queue);
    ASSERT_EQ((uintptr_t)entry, (uintptr_t)remove_elem);
    ASSERT_EQ(0, add_calls);
    ASSERT_EQ(0, entry->expire_high);
    ASSERT_EQ(0, entry->expire_low);
    ASSERT_EQ(status_$ok, status);
}

/* 0xE58E10: a zero reload interval arms a one-shot, flags == 4. */
TEST(set_internal_one_shot_flags)
{
    clock_t interval = { 0, 0 };
    clock_t value = { 0x00000050, 0x0000 };
    clock_t ointerval = { 0, 0 };
    clock_t ovalue = { 0, 0 };
    status_$t status = -1;

    itimer_setup();
    mock_abs_clock = (clock_t){ 0x00000007, 0x0003 };

    time_$set_itimer_internal(0, &interval, &value, &ointerval, &ovalue,
                              &status);

    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(1, add_calls);
    ASSERT_EQ((uintptr_t)&TIME_$RTEQ, (uintptr_t)add_queue);
    ASSERT_EQ(0x00000050, add_when.high);
    ASSERT_EQ(0, add_is_absolute);           /* 0xE58E42 clr.w -(SP) */
    ASSERT_EQ(0x00000007, add_now.high);     /* TIME_$ABS_CLOCK's value */
    ASSERT_EQ(0x0003, add_now.low);
    ASSERT_EQ((uintptr_t)TIME_$SET_ITIMER_REAL_CALLBACK,
              (uintptr_t)add_callback);
    ASSERT_EQ(5, (uintptr_t)add_callback_arg);   /* PROC1_$AS_ID */
    ASSERT_EQ(ITIMER_FLAG_BASE, add_flags);
    ASSERT_EQ(0, add_interval.high);
    ASSERT_EQ((uintptr_t)itimer_entry_for(0, 5), (uintptr_t)add_qelem);
}

/* 0xE58E0C: a non-zero reload interval makes it 4 | 0x12 == 0x16. */
TEST(set_internal_repeating_flags_and_virtual_queue)
{
    clock_t interval = { 0x00000000, 0x0001 };
    clock_t value = { 0x00000050, 0x0000 };
    clock_t ointerval = { 0, 0 };
    clock_t ovalue = { 0, 0 };
    status_$t status = -1;

    itimer_setup();
    mock_cput = (clock_t){ 0x00000011, 0x0000 };

    time_$set_itimer_internal(1, &interval, &value, &ointerval, &ovalue,
                              &status);

    ASSERT_EQ(ITIMER_FLAG_BASE | ITIMER_FLAG_REPEATING, add_flags);
    /* 0xE58D54..0xE58D6C: the VT queue table is 1-based on PROC1_$CURRENT */
    ASSERT_EQ((uintptr_t)&TIME_$VTQ[2], (uintptr_t)add_queue);
    ASSERT_EQ((uintptr_t)&TIME_$VTQ[2], (uintptr_t)remove_queue);
    ASSERT_EQ((uintptr_t)TIME_$SET_ITIMER_VIRT_CALLBACK,
              (uintptr_t)add_callback);
    ASSERT_EQ(0x00000011, add_now.high);     /* PROC1_$GET_CPUT8's value */
    ASSERT_EQ((uintptr_t)itimer_entry_for(1, 5), (uintptr_t)add_qelem);
    ASSERT_EQ(0x0001, add_interval.low);
}

/* ==========================================================================
 * TIME_$GET_ITIMER (0xE58F06) / TIME_$SET_ITIMER (0xE58E58)
 * ========================================================================== */

/*
 * The caller's buffers are handed straight to time_$get_itimer_internal and,
 * for which == 1, doubled in place.
 */
TEST(get_itimer_virtual_doubles_in_place)
{
    uint16_t which = 1;
    clock_t interval = { 0, 0 };
    clock_t value = { 0, 0 };

    itimer_setup();
    itimer_entry_for(1, 5)->flags = ITIMER_FLAG_IN_USE;
    itimer_entry_for(1, 5)->interval_high = 0x00000003;
    itimer_entry_for(1, 5)->interval_low = 0x0001;
    itimer_entry_for(1, 5)->expire_high = 0x00000020;
    itimer_entry_for(1, 5)->expire_low = 0x8000;
    mock_cput = (clock_t){ 0x00000010, 0x0000 };

    TIME_$GET_ITIMER(&which, &interval, &value);

    /* remaining = 0x20:8000 - 0x10:0000, doubled: 0x10*2 + carry(low >= 0x8000) */
    ASSERT_EQ(0x00000021, value.high);
    ASSERT_EQ(0x0000, value.low);
    ASSERT_EQ(0x00000006, interval.high);
    ASSERT_EQ(0x0002, interval.low);
}

TEST(get_itimer_real_leaves_clock_form)
{
    uint16_t which = 0;
    clock_t interval = { 0, 0 };
    clock_t value = { 0, 0 };

    itimer_setup();
    itimer_entry_for(0, 5)->flags = ITIMER_FLAG_IN_USE;
    itimer_entry_for(0, 5)->interval_high = 0x00000003;
    itimer_entry_for(0, 5)->interval_low = 0x0001;
    itimer_entry_for(0, 5)->expire_high = 0x00000010;
    itimer_entry_for(0, 5)->expire_low = 0x8000;
    mock_abs_clock = (clock_t){ 0, 0 };

    TIME_$GET_ITIMER(&which, &interval, &value);

    ASSERT_EQ(0x00000010, value.high);
    ASSERT_EQ(0x8000, value.low);
    ASSERT_EQ(0x00000003, interval.high);
    ASSERT_EQ(0x0001, interval.low);
}

/*
 * Incoming values are halved, the caller's old-value buffers are filled from
 * the database and then doubled in place.
 */
TEST(set_itimer_virtual_halves_in_doubles_out)
{
    uint16_t which = 1;
    clock_t interval = { 0x00000006, 0x0002 };
    clock_t value = { 0x00000021, 0x0000 };
    clock_t ointerval = { 0, 0 };
    clock_t ovalue = { 0, 0 };
    status_$t status = -1;

    itimer_setup();
    itimer_entry_for(1, 5)->flags = ITIMER_FLAG_IN_USE;
    itimer_entry_for(1, 5)->interval_high = 0x00000002;
    itimer_entry_for(1, 5)->interval_low = 0x0001;
    itimer_entry_for(1, 5)->expire_high = 0x00000110;
    mock_cput = (clock_t){ 0x00000010, 0x0000 };

    TIME_$SET_ITIMER(&which, &interval, &value, &ointerval, &ovalue, &status);

    /* halved on the way in */
    ASSERT_EQ(0x00000010, add_when.high);
    ASSERT_EQ(0x8000, add_when.low);
    ASSERT_EQ(0x00000003, add_interval.high);
    ASSERT_EQ(0x0001, add_interval.low);
    /* doubled on the way out */
    ASSERT_EQ(0x00000200, ovalue.high);
    ASSERT_EQ(0x0000, ovalue.low);
    ASSERT_EQ(0x00000004, ointerval.high);
    ASSERT_EQ(0x0002, ointerval.low);
    /* the caller's arguments are not disturbed */
    ASSERT_EQ(0x00000021, value.high);
    ASSERT_EQ(0x00000006, interval.high);
}

TEST(set_itimer_real_passes_through)
{
    uint16_t which = 0;
    clock_t interval = { 0x00000006, 0x0002 };
    clock_t value = { 0x00000021, 0x0003 };
    clock_t ointerval = { 0, 0 };
    clock_t ovalue = { 0, 0 };
    status_$t status = -1;

    itimer_setup();
    itimer_entry_for(0, 5)->flags = ITIMER_FLAG_IN_USE;
    itimer_entry_for(0, 5)->interval_high = 0x00000002;
    itimer_entry_for(0, 5)->interval_low = 0x0001;
    itimer_entry_for(0, 5)->expire_high = 0x00000100;
    itimer_entry_for(0, 5)->expire_low = 0x8000;
    mock_abs_clock = (clock_t){ 0, 0 };

    TIME_$SET_ITIMER(&which, &interval, &value, &ointerval, &ovalue, &status);

    /* no conversion on the way in */
    ASSERT_EQ(0x00000021, add_when.high);
    ASSERT_EQ(0x0003, add_when.low);
    ASSERT_EQ(0x00000006, add_interval.high);
    /* nor on the way out */
    ASSERT_EQ(0x00000100, ovalue.high);
    ASSERT_EQ(0x8000, ovalue.low);
    ASSERT_EQ(0x00000002, ointerval.high);
}

/* ==========================================================================
 * TIME_$SET_CPU_LIMIT
 * ========================================================================== */

static void cpu_limit_setup(void)
{
    memset(cpu_limit_arena, 0, sizeof(cpu_limit_arena));
    memset(TIME_$VTQ, 0, sizeof(TIME_$VTQ));
    ARCH_HOST_VA_BASE = (uintptr_t)cpu_limit_arena - CPU_LIMIT_DB_BASE;
    PROC1_$CURRENT = 3;
    PROC1_$AS_ID = 5;
    remove_calls = add_calls = signal_calls = 0;
}

static time_queue_elem_t *cpu_entry_for(uint16_t as_id)
{
    return (time_queue_elem_t *)(cpu_limit_arena +
                                 as_id * CPU_LIMIT_DB_ENTRY_SIZE);
}

/* A zero limit clears the entry and arms nothing (0xE58FEE..0xE5901A). */
TEST(set_cpu_limit_zero_clears)
{
    clock_t limit = { 0, 0 };
    boolean relative = false;
    status_$t status = -1;

    cpu_limit_setup();
    mock_cput = (clock_t){ 0x00000050, 0x0000 };
    cpu_entry_for(5)->expire_high = 0x11111111;
    cpu_entry_for(5)->expire_low = 0x2222;

    TIME_$SET_CPU_LIMIT(&limit, &relative, &status);

    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ((uintptr_t)&TIME_$VTQ[2], (uintptr_t)remove_queue);
    ASSERT_EQ((uintptr_t)cpu_entry_for(5), (uintptr_t)remove_elem);
    ASSERT_EQ(0, add_calls);
    ASSERT_EQ(0, signal_calls);
    ASSERT_EQ(0, cpu_entry_for(5)->expire_high);
    ASSERT_EQ(0, cpu_entry_for(5)->expire_low);
    ASSERT_EQ(status_$ok, status);
}

/*
 * A relative limit always schedules, with is_absolute == 0, and the `when`
 * handed to the queue is the HALVED limit (0xE58FA0/0xE590A4).
 */
TEST(set_cpu_limit_relative_schedules_halved)
{
    clock_t limit = { 0x00000101, 0x0002 };
    boolean relative = true;
    status_$t status = -1;

    cpu_limit_setup();
    mock_cput = (clock_t){ 0x00000050, 0x0000 };

    TIME_$SET_CPU_LIMIT(&limit, &relative, &status);

    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(0, signal_calls);
    ASSERT_EQ(0, add_is_absolute);
    ASSERT_EQ((uintptr_t)&TIME_$VTQ[2], (uintptr_t)add_queue);
    ASSERT_EQ(0x00000080, add_when.high);   /* 0x101 >> 1 */
    ASSERT_EQ(0x8001, add_when.low);        /* bit 0 of high -> bit 15 */
    ASSERT_EQ(0x00000050, add_now.high);
    ASSERT_EQ(0, add_interval.high);
    ASSERT_EQ(0, add_interval.low);
    ASSERT_EQ(4, add_flags);
    ASSERT_EQ(5, (uintptr_t)add_callback_arg);
    ASSERT_EQ((uintptr_t)cpu_entry_for(5), (uintptr_t)add_qelem);
    /* the caller's limit is left alone: SUB48 works on a scratch copy */
    ASSERT_EQ(0x00000101, limit.high);
    ASSERT_EQ(0x0002, limit.low);
}

/*
 * An absolute limit still in the future schedules with is_absolute == 1.
 * SUB48 compares the RAW limit against the CPU clock (0xE59032), not the
 * halved copy - with the halved copy this case would signal instead.
 */
TEST(set_cpu_limit_absolute_future_schedules)
{
    clock_t limit = { 0x00000101, 0x0002 };
    boolean relative = false;
    status_$t status = -1;

    cpu_limit_setup();
    /* between limit/2 (0x80.8001) and limit (0x101.0002) */
    mock_cput = (clock_t){ 0x000000C0, 0x0000 };

    TIME_$SET_CPU_LIMIT(&limit, &relative, &status);

    ASSERT_EQ(0, signal_calls);
    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(1, add_is_absolute);
    ASSERT_EQ(0x00000080, add_when.high);
    ASSERT_EQ(0x8001, add_when.low);
}

/* An absolute limit already reached signals with the 0xE58B52/0xE58B54 cells */
TEST(set_cpu_limit_absolute_past_signals)
{
    clock_t limit = { 0x00000101, 0x0002 };
    boolean relative = false;
    status_$t status = -1;

    cpu_limit_setup();
    mock_cput = (clock_t){ 0x00000200, 0x0000 };

    TIME_$SET_CPU_LIMIT(&limit, &relative, &status);

    ASSERT_EQ(1, signal_calls);
    ASSERT_EQ(0, add_calls);
    ASSERT_EQ((uintptr_t)&PROC2_$UID[5], (uintptr_t)signal_uid);
    ASSERT_EQ(0x001B, signal_number);
    ASSERT_EQ(0x000D000B, signal_param);
}

int main(void)
{
    printf("=== TIME interval-timer tests ===\n");

    RUN_TEST(clock_to_itimer_direction_and_carry);
    RUN_TEST(clock_to_itimer_no_carry_below_8000);
    RUN_TEST(clock_to_itimer_high_is_logical);
    RUN_TEST(itimer_to_clock_direction_and_borrow);
    RUN_TEST(itimer_to_clock_shift_is_logical);
    RUN_TEST(shift_round_trip);
    RUN_TEST(get_internal_not_in_use_returns_zeros);
    RUN_TEST(get_internal_real_returns_remaining_time);
    RUN_TEST(get_internal_expired_value_is_zeroed);
    RUN_TEST(get_internal_virtual_uses_cput_and_second_half);
    RUN_TEST(set_internal_rejects_too_large_value);
    RUN_TEST(set_internal_rejects_too_large_interval);
    RUN_TEST(set_internal_zero_value_disarms);
    RUN_TEST(set_internal_one_shot_flags);
    RUN_TEST(set_internal_repeating_flags_and_virtual_queue);
    RUN_TEST(get_itimer_virtual_doubles_in_place);
    RUN_TEST(get_itimer_real_leaves_clock_form);
    RUN_TEST(set_itimer_virtual_halves_in_doubles_out);
    RUN_TEST(set_itimer_real_passes_through);
    RUN_TEST(set_cpu_limit_zero_clears);
    RUN_TEST(set_cpu_limit_relative_schedules_halved);
    RUN_TEST(set_cpu_limit_absolute_future_schedules);
    RUN_TEST(set_cpu_limit_absolute_past_signals);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
