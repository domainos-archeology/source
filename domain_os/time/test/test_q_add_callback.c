/*
 * time/test/test_q_add_callback.c - Unit tests for TIME_$Q_ADD_CALLBACK
 * (0x00E16DD4) and TIME_$Q_REENTER_ELEM (0x00E16C8E).
 *
 * Both routines take a `when` and a separate `now`, and both had the two
 * crossed before this pass: TIME_$Q_ADD_CALLBACK added `when` to itself and
 * handed `when` to TIME_$Q_ENTER_ELEM where 0x00E16E0C and 0x00E16E34 use
 * `now`, and TIME_$Q_REENTER_ELEM dropped the timer re-arm at
 * 0x00E16D22-0x00E16D44 entirely.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    test_##name();                                                            \
    tests_passed++;                                                           \
    printf("done\n");                                                         \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    unsigned long long _e = (unsigned long long)(expected);                   \
    unsigned long long _a = (unsigned long long)(actual);                     \
    if (_e != _a) {                                                           \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",      \
               _e, _a, __LINE__);                                             \
        tests_failed++;                                                       \
        return;                                                               \
    }                                                                         \
} while (0)

#include "time/time_internal.h"

/* Globals the code under test links against. */
uint8_t IN_VT_INT;
uint8_t IN_RT_INT;

/* Mocked callees. */
void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t low = (uint32_t)dst->low + (uint32_t)src->low;
    dst->low = (uint16_t)low;
    dst->high = dst->high + src->high + (low >> 16);
}

static int enter_calls;
static time_queue_t *enter_queue;
static clock_t enter_now;
static time_queue_elem_t *enter_elem;

void TIME_$Q_ENTER_ELEM(time_queue_t *queue, clock_t *now,
                        time_queue_elem_t *elem, status_$t *status)
{
    enter_calls++;
    enter_queue = queue;
    enter_now = *now;
    enter_elem = elem;
    *status = status_$ok;
}

static int remove_calls;
static status_$t remove_status;

void time_$q_remove_internal(time_queue_t *queue, time_queue_elem_t *elem,
                             status_$t *status)
{
    (void)queue; (void)elem;
    remove_calls++;
    *status = remove_status;
}

static int insert_calls;
static int8_t insert_result;

int8_t time_$q_insert_sorted(time_queue_t *queue, time_queue_elem_t *elem)
{
    (void)queue; (void)elem;
    insert_calls++;
    return insert_result;
}

static int setup_timer_calls;
static time_queue_t *setup_timer_queue;
static clock_t setup_timer_now;

void time_$q_setup_timer(time_queue_t *queue, clock_t *when)
{
    setup_timer_calls++;
    setup_timer_queue = queue;
    setup_timer_now = *when;
}

static int spin_locks;
static int spin_unlocks;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    spin_locks++;
    return 0x0055;
}

void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)
{
    ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token;
    (void)lockp;
    spin_unlocks++;
    if (token != 0x0055) { printf("BAD TOKEN "); tests_failed++; }
}

/*
 * time/q_add_callback.c stores the callback through DXM_$CALLBACK_CELL,
 * which on the host is a registry call (dxm/dxm.h).  The registry itself is
 * not under test, so it is stubbed the way tty/test/test_tty_data.c does.
 */
#include "dxm/dxm.h"

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    return (dxm_$callback_t)(fn != NULL);
}

#include "../q_add_callback.c"
#include "../q_reenter_elem.c"

static time_queue_t queue;
static time_queue_elem_t elem;

static void reset(void)
{
    memset(&queue, 0, sizeof(queue));
    memset(&elem, 0, sizeof(elem));
    enter_calls = remove_calls = insert_calls = 0;
    setup_timer_calls = spin_locks = spin_unlocks = 0;
    remove_status = status_$ok;
    insert_result = 0;
    IN_VT_INT = 0;
    IN_RT_INT = 0;
}

/* 0xE16E02: a zero flag means `when` is relative to `now`. */
TEST(add_callback_relative_adds_now)
{
    clock_t when = { 0x00000010, 0x8000 };
    clock_t now  = { 0x00000100, 0x8000 };
    clock_t interval = { 0x00000007, 0x0009 };
    status_$t status = -1;

    reset();
    TIME_$Q_ADD_CALLBACK(&queue, &when, 0, &now,
                         (void *)0x1234, (void *)0x5678, 0x000F,
                         &interval, &elem, &status);

    /* 0x10.8000 + 0x100.8000 = 0x111.0000 */
    ASSERT_EQ(0x00000111u, elem.expire_high);
    ASSERT_EQ(0x0000, elem.expire_low);
    ASSERT_EQ(0x000F, elem.flags);
    ASSERT_EQ(0x00000007u, elem.interval_high);
    ASSERT_EQ(0x0009, elem.interval_low);
    ASSERT_EQ(1, enter_calls);
    /* 0xE16E34: ENTER_ELEM gets `now`, not `when` */
    ASSERT_EQ(0x00000100u, enter_now.high);
    ASSERT_EQ(0x8000, enter_now.low);
    ASSERT_EQ(status_$ok, status);
}

/* A non-zero flag leaves the expiry alone but still passes `now` on. */
TEST(add_callback_absolute_leaves_the_expiry)
{
    clock_t when = { 0x00000010, 0x8000 };
    clock_t now  = { 0x00000100, 0x8000 };
    clock_t interval = { 0, 0 };
    status_$t status = -1;

    reset();
    TIME_$Q_ADD_CALLBACK(&queue, &when, 1, &now,
                         (void *)0x1234, (void *)0x5678, 4,
                         &interval, &elem, &status);

    ASSERT_EQ(0x00000010u, elem.expire_high);
    ASSERT_EQ(0x8000, elem.expire_low);
    ASSERT_EQ(0x00000100u, enter_now.high);
    /* the caller's `when` must be untouched */
    ASSERT_EQ(0x00000010u, when.high);
}

/* 0xE16CCA: three statuses count as "removed". */
TEST(reenter_treats_three_statuses_as_success)
{
    clock_t when = { 0x00000010, 0x0000 };
    clock_t base = { 0x00000001, 0x0000 };
    status_$t status = -1;
    const status_$t ok_codes[3] = {
        status_$ok,
        status_$time_queue_element_not_found,
        status_$time_queue_element_not_in_use
    };
    int i;

    for (i = 0; i < 3; i++) {
        reset();
        remove_status = ok_codes[i];
        TIME_$Q_REENTER_ELEM(&queue, &when, 1, &base, &elem, &status);
        ASSERT_EQ(status_$ok, status);
        ASSERT_EQ(1, insert_calls);
        ASSERT_EQ(1, spin_unlocks);
    }

    /* anything else is reported and stops the re-entry */
    reset();
    remove_status = status_$time_bad_timer_key;
    TIME_$Q_REENTER_ELEM(&queue, &when, 1, &base, &elem, &status);
    ASSERT_EQ(status_$time_bad_timer_key, status);
    ASSERT_EQ(0, insert_calls);
    ASSERT_EQ(1, spin_unlocks);
}

/* 0xE16D22: only an insertion at the head re-arms the hardware. */
TEST(reenter_arms_the_timer_only_for_a_new_head)
{
    clock_t when = { 0x00000010, 0x0000 };
    clock_t base = { 0x00000001, 0x0000 };
    status_$t status = -1;

    reset();
    insert_result = 0;              /* not the head */
    TIME_$Q_REENTER_ELEM(&queue, &when, 1, &base, &elem, &status);
    ASSERT_EQ(0, setup_timer_calls);

    reset();
    insert_result = -1;             /* became the head */
    TIME_$Q_REENTER_ELEM(&queue, &when, 1, &base, &elem, &status);
    ASSERT_EQ(1, setup_timer_calls);
    ASSERT_EQ((uintptr_t)&queue, (uintptr_t)setup_timer_queue);
    /* 0xE16D3C: setup_timer gets `base_time`, not `when` */
    ASSERT_EQ(0x00000001u, setup_timer_now.high);
}

/*
 * 0xE16D26 - 0xE16D3A: queue->flags picks the interrupt flag, and being
 * inside that handler suppresses the re-arm.
 */
TEST(reenter_respects_the_in_interrupt_flags)
{
    clock_t when = { 0x00000010, 0x0000 };
    clock_t base = { 0x00000001, 0x0000 };
    status_$t status = -1;

    /* a negative flags byte selects the virtual timer */
    reset();
    insert_result = -1;
    queue.flags = 0xFF;
    IN_VT_INT = 0xFF;
    IN_RT_INT = 0;
    TIME_$Q_REENTER_ELEM(&queue, &when, 1, &base, &elem, &status);
    ASSERT_EQ(0, setup_timer_calls);

    reset();
    insert_result = -1;
    queue.flags = 0xFF;
    IN_VT_INT = 0;
    IN_RT_INT = 0xFF;               /* the wrong flag must not suppress it */
    TIME_$Q_REENTER_ELEM(&queue, &when, 1, &base, &elem, &status);
    ASSERT_EQ(1, setup_timer_calls);

    /* a non-negative flags byte selects the real-time timer */
    reset();
    insert_result = -1;
    queue.flags = 0;
    IN_RT_INT = 0xFF;
    TIME_$Q_REENTER_ELEM(&queue, &when, 1, &base, &elem, &status);
    ASSERT_EQ(0, setup_timer_calls);
}

/* 0xE16D06: qflags == 0 adds base_time to the expiry. */
TEST(reenter_relative_adds_the_base_time)
{
    clock_t when = { 0x00000010, 0x8000 };
    clock_t base = { 0x00000100, 0x8000 };
    status_$t status = -1;

    reset();
    TIME_$Q_REENTER_ELEM(&queue, &when, 0, &base, &elem, &status);

    ASSERT_EQ(0x00000111u, elem.expire_high);
    ASSERT_EQ(0x0000, elem.expire_low);
}

int main(void)
{
    printf("=== TIME queue-entry tests ===\n");

    RUN_TEST(add_callback_relative_adds_now);
    RUN_TEST(add_callback_absolute_leaves_the_expiry);
    RUN_TEST(reenter_treats_three_statuses_as_success);
    RUN_TEST(reenter_arms_the_timer_only_for_a_new_head);
    RUN_TEST(reenter_respects_the_in_interrupt_flags);
    RUN_TEST(reenter_relative_adds_the_base_time);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}
