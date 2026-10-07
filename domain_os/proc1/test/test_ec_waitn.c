/*
 * Tests for PROC1_$EC_WAITN (0x00E2065A).
 *
 * Includes the real proc1/ec_waitn.c and drives it through mocked
 * proc1_$remove_from_ready_list / PROC1_$DISPATCH_INT2.  The dispatch mock
 * can advance an eventcount to model the wake-up, and it records the
 * waiter lists as they stood while the process was "blocked".
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Module cells                                                        */
/* ------------------------------------------------------------------ */

ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static int n_remove;
static proc1_t *removed_pcb;
static int ipl_at_remove;

void proc1_$remove_from_ready_list(proc1_t *pcb)
{
    n_remove++;
    removed_pcb = pcb;
    ipl_at_remove = __host_intr_disable_count;
}

#define MAX_ECS 8

static int n_dispatch;
static proc1_t *dispatched_pcb;
static int ipl_at_dispatch;
static uint8_t pri_max_at_dispatch;
static uint32_t wait_start_at_dispatch;
static int removes_at_dispatch;

/* what the dispatch mock does to model the wake-up */
static ec_$eventcount_t *advance_ec;
static int32_t advance_by;

/* snapshot of every waited eventcount's list while blocked */
static ec_$eventcount_t *watched[MAX_ECS];
static int n_watched;
static ec_$eventcount_waiter_t *head_at_dispatch[MAX_ECS];
static int32_t cell_val_at_dispatch[MAX_ECS];
static void *cell_pcb_at_dispatch[MAX_ECS];
static ec_$eventcount_waiter_t *cell_next_at_dispatch[MAX_ECS];
static ec_$eventcount_waiter_t *cell_prev_at_dispatch[MAX_ECS];
static ec_$eventcount_waiter_t *prev_next_at_dispatch[MAX_ECS];

void PROC1_$DISPATCH_INT2(proc1_t *pcb)
{
    int i;

    n_dispatch++;
    dispatched_pcb = pcb;
    ipl_at_dispatch = __host_intr_disable_count;
    pri_max_at_dispatch = pcb->pri_max;
    wait_start_at_dispatch = pcb->wait_start;
    removes_at_dispatch = n_remove;

    for (i = 0; i < n_watched; i++) {
        ec_$eventcount_waiter_t *cell = watched[i]->waiter_list_head;
        head_at_dispatch[i] = cell;
        cell_val_at_dispatch[i] = cell->wait_val;
        cell_pcb_at_dispatch[i] = cell->pcb;
        cell_next_at_dispatch[i] = cell->next_waiter;
        cell_prev_at_dispatch[i] = cell->prev_waiter;
        prev_next_at_dispatch[i] = cell->prev_waiter->next_waiter;
    }

    if (advance_ec != NULL) {
        advance_ec->value += advance_by;
    }
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "../ec_waitn.c"

/* ------------------------------------------------------------------ */

static int tests_run, tests_failed;

#define ASSERT_EQ(a, b) do {                                                  \
    unsigned long long _a = (unsigned long long)(a);                          \
    unsigned long long _b = (unsigned long long)(b);                          \
    if (_a != _b) {                                                           \
        printf("  FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__,       \
               __LINE__, #a, #b, _a, _b);                                     \
        tests_failed++;                                                       \
    }                                                                         \
} while (0)

#define ASSERT_PTR_EQ(a, b) ASSERT_EQ((uintptr_t)(a), (uintptr_t)(b))

#define RUN_TEST(fn) do { tests_run++; reset(); fn(); } while (0)

static proc1_t pcb;
static ec_$eventcount_t ecs_storage[MAX_ECS];
static ec_$eventcount_t *ecs[MAX_ECS];
static int32_t vals[MAX_ECS];

/* EC_$INIT (0x00E151FE): value 0, both links back at the eventcount */
static void ec_init(ec_$eventcount_t *ec)
{
    ec->value = 0;
    ec->waiter_list_head = (ec_$eventcount_waiter_t *)ec;
    ec->waiter_list_tail = (ec_$eventcount_waiter_t *)ec;
}

static int ec_is_empty(const ec_$eventcount_t *ec)
{
    return ec->waiter_list_head == (const ec_$eventcount_waiter_t *)ec &&
           ec->waiter_list_tail == (const ec_$eventcount_waiter_t *)ec;
}

static void reset(void)
{
    int i;
    memset(&pcb, 0, sizeof(pcb));
    pcb.pri_max = PROC1_FLAG_BOUND;
    pcb.wait_start = 0xDEADBEEF;
    for (i = 0; i < MAX_ECS; i++) {
        ec_init(&ecs_storage[i]);
        ecs[i] = &ecs_storage[i];
        vals[i] = 0;
    }
    TIME_$CLOCKH = 0x01020304;
    __host_intr_disable_count = 3;      /* an arbitrary incoming IPL */
    n_remove = 0;
    n_dispatch = 0;
    advance_ec = NULL;
    advance_by = 0;
    n_watched = 0;
}

static void watch(int count)
{
    int i;
    n_watched = count;
    for (i = 0; i < count; i++) {
        watched[i] = ecs[i];
    }
}

/* 0x00E20664: count 0 exits at once with D0 = count, IPL forced to 0 */
static void test_count_zero(void)
{
    uint16_t r = PROC1_$EC_WAITN(&pcb, ecs, vals, 0);

    ASSERT_EQ(r, 0);
    ASSERT_EQ(__host_intr_disable_count, 0);
    ASSERT_EQ(n_remove, 0);
    ASSERT_EQ(n_dispatch, 0);
    ASSERT_EQ(ec_is_empty(ecs[0]), 1);
}

/* 0x00E20664 / 0x00E206CA: a negative count comes back as itself */
static void test_count_negative(void)
{
    uint16_t r = PROC1_$EC_WAITN(&pcb, ecs, vals, -1);

    ASSERT_EQ(r, 0xFFFF);
    ASSERT_EQ(n_dispatch, 0);
}

/*
 * 0x00E20682..0x00E2068A: the first eventcount already at the value
 * asked for satisfies the wait without blocking; the cell is linked and
 * unlinked, and the answer is 1.
 */
static void test_first_already_satisfied(void)
{
    uint16_t r;

    ecs[0]->value = 5;
    vals[0] = 5;
    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 1);

    ASSERT_EQ(r, 1);
    ASSERT_EQ(n_remove, 0);
    ASSERT_EQ(n_dispatch, 0);
    ASSERT_EQ(ec_is_empty(ecs[0]), 1);
    ASSERT_EQ(pcb.pri_max, PROC1_FLAG_BOUND);
    ASSERT_EQ(pcb.wait_start, 0xDEADBEEF);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* a value already exceeded (D1 < 0) is satisfied just like an equal one */
static void test_value_already_passed(void)
{
    uint16_t r;

    ecs[0]->value = 9;
    vals[0] = 5;
    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 1);

    ASSERT_EQ(r, 1);
    ASSERT_EQ(n_dispatch, 0);
}

/*
 * 0x00E20686 / 0x00E2068A: the linking scan stops at the first satisfied
 * eventcount - the third one is never linked - and the answer is 2.
 */
static void test_second_satisfied_stops_scan(void)
{
    uint16_t r;

    vals[0] = 1;                        /* ec0 at 0: not yet */
    ecs[1]->value = 4; vals[1] = 4;     /* ec1: satisfied */
    vals[2] = 1;                        /* ec2: would not be, never looked at */
    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 3);

    ASSERT_EQ(r, 2);
    ASSERT_EQ(n_dispatch, 0);
    ASSERT_EQ(ec_is_empty(ecs[0]), 1);
    ASSERT_EQ(ec_is_empty(ecs[1]), 1);
    ASSERT_EQ(ec_is_empty(ecs[2]), 1);
}

/*
 * 0x00E2068C..0x00E2069C: nothing satisfied - the process leaves the
 * ready list, sets WAITING, stamps wait_start with TIME_$CLOCKH and
 * dispatches, all at the raised IPL.  While blocked every eventcount's
 * head is a cell holding {value, prev = old head, next = ec, pcb}, and
 * the old head's next points at the cell (0x00E2067A / 0x00E2067E).
 */
static void test_blocks_and_links_cells(void)
{
    uint16_t r;
    int i;

    vals[0] = 3;
    vals[1] = 7;
    watch(2);
    advance_ec = ecs[1];
    advance_by = 7;

    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 2);

    ASSERT_EQ(n_remove, 1);
    ASSERT_PTR_EQ(removed_pcb, &pcb);
    ASSERT_EQ(ipl_at_remove, 4);
    ASSERT_EQ(n_dispatch, 1);
    ASSERT_PTR_EQ(dispatched_pcb, &pcb);
    ASSERT_EQ(ipl_at_dispatch, 4);
    ASSERT_EQ(removes_at_dispatch, 1);
    ASSERT_EQ(pri_max_at_dispatch, PROC1_FLAG_BOUND | PROC1_FLAG_WAITING);
    ASSERT_EQ(wait_start_at_dispatch, 0x01020304);

    for (i = 0; i < 2; i++) {
        ASSERT_EQ(cell_val_at_dispatch[i], vals[i]);
        ASSERT_PTR_EQ(cell_pcb_at_dispatch[i], &pcb);
        ASSERT_PTR_EQ(cell_next_at_dispatch[i], ecs[i]);
        ASSERT_PTR_EQ(cell_prev_at_dispatch[i], ecs[i]);
        ASSERT_PTR_EQ(prev_next_at_dispatch[i], head_at_dispatch[i]);
    }
    /* the two cells are distinct */
    ASSERT_EQ(head_at_dispatch[0] != head_at_dispatch[1], 1);

    /* woken by ec1 reaching 7: answer 2, lists restored, IPL forced 0 */
    ASSERT_EQ(r, 2);
    ASSERT_EQ(ec_is_empty(ecs[0]), 1);
    ASSERT_EQ(ec_is_empty(ecs[1]), 1);
    ASSERT_EQ(__host_intr_disable_count, 0);
    /* the WAITING bit is somebody else's to clear */
    ASSERT_EQ(pcb.pri_max, PROC1_FLAG_BOUND | PROC1_FLAG_WAITING);
}

/* a cell is linked BEHIND an existing waiter: prev = old head */
static void test_links_behind_existing_waiter(void)
{
    ec_$eventcount_waiter_t other;
    uint16_t r;

    other.wait_val = 99;
    other.prev_waiter = (ec_$eventcount_waiter_t *)ecs[0];
    other.next_waiter = (ec_$eventcount_waiter_t *)ecs[0];
    other.pcb = NULL;
    ecs[0]->waiter_list_head = &other;
    ecs[0]->waiter_list_tail = &other;

    vals[0] = 1;
    watch(1);
    advance_ec = ecs[0];
    advance_by = 1;

    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 1);

    ASSERT_EQ(r, 1);
    ASSERT_PTR_EQ(cell_prev_at_dispatch[0], &other);
    ASSERT_PTR_EQ(cell_next_at_dispatch[0], ecs[0]);
    ASSERT_PTR_EQ(prev_next_at_dispatch[0], head_at_dispatch[0]);
    /* 0x00E206AC / 0x00E206B0: restored exactly */
    ASSERT_PTR_EQ(ecs[0]->waiter_list_head, &other);
    ASSERT_PTR_EQ(other.next_waiter, ecs[0]);
    ASSERT_PTR_EQ(ecs[0]->waiter_list_tail, &other);
}

/*
 * 0x00E206BA..0x00E206C2: after the wake-up every cell is examined; the
 * answer is the LOWEST satisfied index even when a higher one is also
 * satisfied.
 */
static void test_lowest_satisfied_index_wins(void)
{
    uint16_t r;

    vals[0] = 1;
    vals[1] = 2;
    vals[2] = 3;
    watch(3);
    /* wake with ec1 satisfied, and also satisfy ec2 and ec0 by hand */
    advance_ec = ecs[1];
    advance_by = 2;

    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 3);
    ASSERT_EQ(r, 2);
    ASSERT_EQ(ec_is_empty(ecs[0]), 1);
    ASSERT_EQ(ec_is_empty(ecs[2]), 1);

    reset();
    vals[0] = 1;
    vals[1] = 2;
    vals[2] = 3;
    advance_ec = ecs[2];
    advance_by = 3;
    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 3);
    ASSERT_EQ(r, 3);

    reset();
    vals[0] = 1;
    vals[1] = 2;
    advance_ec = ecs[0];
    advance_by = 5;
    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 2);
    ASSERT_EQ(r, 1);
}

/* a spurious wake-up with nothing satisfied answers 0 and unlinks all */
static void test_spurious_wakeup_returns_zero(void)
{
    uint16_t r;

    vals[0] = 1;
    vals[1] = 1;
    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 2);

    ASSERT_EQ(n_dispatch, 1);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(ec_is_empty(ecs[0]), 1);
    ASSERT_EQ(ec_is_empty(ecs[1]), 1);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* the same eventcount waited on twice links two cells in order */
static void test_same_ec_twice(void)
{
    uint16_t r;

    ecs[1] = ecs[0];
    vals[0] = 1;
    vals[1] = 2;
    advance_ec = ecs[0];
    advance_by = 1;
    r = PROC1_$EC_WAITN(&pcb, ecs, vals, 2);

    ASSERT_EQ(r, 1);
    ASSERT_EQ(ec_is_empty(ecs[0]), 1);
}

int main(void)
{
    RUN_TEST(test_count_zero);
    RUN_TEST(test_count_negative);
    RUN_TEST(test_first_already_satisfied);
    RUN_TEST(test_value_already_passed);
    RUN_TEST(test_second_satisfied_stops_scan);
    RUN_TEST(test_blocks_and_links_cells);
    RUN_TEST(test_links_behind_existing_waiter);
    RUN_TEST(test_lowest_satisfied_index_wins);
    RUN_TEST(test_spurious_wakeup_returns_zero);
    RUN_TEST(test_same_ec_twice);

    printf("test_ec_waitn: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
