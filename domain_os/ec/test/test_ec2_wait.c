/*
 * ec/test/test_ec2_wait.c - Unit tests for EC2_$WAIT (0x00E42358) and
 * EC2_$WAKEUP (0x00E4285A).
 *
 * The real ec/ec_data.c cells and EC2_$INIT_S are used to build the waiter
 * free list.  ML_$LOCK/ML_$UNLOCK, EC_$INIT, EC_$ADVANCE, EC_$WAITN and the
 * FIM cleanup calls are mocked; EC_$WAITN is scripted so every branch of the
 * scan / wait / cleanup rounds can be driven.
 *
 * EC2 "handles" are 32-bit virtual addresses in the original, so the test
 * points ARCH_HOST_VA_BASE at an arena and places its eventcounts inside it:
 * an eventcount at arena + 0x1000 then has handle 0x1000, an index handle is
 * ARCH_VA_TO_PTR(n), and AS_$PROTECTION sits at arena + 0x8000.
 */

#include "ec/ec_internal.h"
#include "fim/fim.h"
#include "proc1/proc1.h"
#include "ml/ml.h"

#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Tiny test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    unsigned long _a = (unsigned long)(a); \
    unsigned long _b = (unsigned long)(b); \
    if (_a != _b) { \
        printf("FAILED\n    %s = 0x%lx (%lu), %s = 0x%lx (%lu) at line %d\n", \
               #a, _a, _a, #b, _b, _b, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s is false at line %d\n", #cond, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
uint32_t FIM_$QUIT_VALUE[64];
ec_$eventcount_t FIM_$QUIT_EC[64];
void *AS_$PROTECTION;

static uint8_t arena[0x10000];

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int lock_calls, unlock_calls, lock_depth;
static int16_t last_lock_id;

void ML_$LOCK(int16_t resource_id)
{
    lock_calls++;
    last_lock_id = resource_id;
    lock_depth++;
}

void ML_$UNLOCK(int16_t resource_id)
{
    unlock_calls++;
    last_lock_id = resource_id;
    lock_depth--;
}

void EC_$INIT(ec_$eventcount_t *ec)
{
    ec->value = 0;
}

static int advance_calls;
static ec_$eventcount_t *advance_ecs[8];

void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    if (advance_calls < 8) {
        advance_ecs[advance_calls] = ec;
    }
    advance_calls++;
}

static status_$t fim_cleanup_result;
static int rls_cleanup_calls, fim_signal_calls;
static status_$t fim_signal_status;

status_$t FIM_$CLEANUP(void *handler)
{
    (void)handler;
    return fim_cleanup_result;
}

void FIM_$RLS_CLEANUP(void *cleanup_data)
{
    (void)cleanup_data;
    rls_cleanup_calls++;
}

void FIM_$SIGNAL(status_$t status)
{
    fim_signal_calls++;
    fim_signal_status = status;
}

/*
 * Scripted EC_$WAITN: snapshots its arguments, runs an optional hook (which
 * plays the role of another process advancing something) and returns the
 * next scripted result.
 */
static int waitn_calls;
static ec_$eventcount_t *waitn_ecs[0x20];
static int32_t waitn_vals[0x20];
static int16_t waitn_n;
static int waitn_lock_depth;
static uint16_t waitn_results[4];
static int waitn_result_i;
static void (*waitn_hook)(void);
static int16_t waitn_awaiters_seen;     /* ec2 waiter word during the wait */
static ec2_$eventcount_t *waitn_watch;  /* whose waiter word to sample     */
static ec2_waiter_t waitn_record_seen;  /* a copy of that waiter record    */

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    int k;
    waitn_calls++;
    waitn_n = num_ecs;
    waitn_lock_depth = lock_depth;
    for (k = 0; k < num_ecs && k < 0x20; k++) {
        waitn_ecs[k] = ecs[k];
        waitn_vals[k] = wait_val[k];
    }
    if (waitn_watch != NULL) {
        waitn_awaiters_seen = waitn_watch->awaiters;
        if (waitn_awaiters_seen > 0 && waitn_awaiters_seen <= 0xE0) {
            waitn_record_seen = EC2_WAITER_TABLE_BASE[waitn_awaiters_seen];
        }
    }
    if (waitn_hook != NULL) {
        waitn_hook();
    }
    return waitn_results[waitn_result_i++];
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../ec_data.c"
#include "../ec2_init_s.c"
#include "../ec2_wait.c"
#include "../ec2_wakeup.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static ec2_$eventcount_t *ec_a, *ec_b;   /* arena + 0x1000, + 0x1100 */
static ec_$eventcount_t reg_ec;          /* a registered level-1 EC  */

static int free_list_length(void)
{
    int n = 0;
    uint16_t idx = DAT_00e7cf08;
    while (idx != 0 && n < 1000) {
        n++;
        idx = (uint16_t)EC2_WAITER_TABLE_BASE[idx].next;
    }
    return n;
}

static void reset(void)
{
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    AS_$PROTECTION = arena + 0x8000;
    ec_a = (ec2_$eventcount_t *)(arena + 0x1000);
    ec_b = (ec2_$eventcount_t *)(arena + 0x1100);

    memset(EC2_$WAIT_ECS, 0, sizeof(EC2_$WAIT_ECS));
    memset(EC2_$PBU_ECS, 0, sizeof(EC2_$PBU_ECS));
    memset(DAT_00e7caf8, 0, sizeof(DAT_00e7caf8));
    EC2_$INIT_S();
    DAT_00e7cf06 = 0;

    /* register one level-1 eventcount as index 2 */
    reg_ec.value = 0;
    _DAT_00e7cf04 = 2;
    DAT_00e7caf8[2] = &reg_ec;

    PROC1_$CURRENT = 3;
    PROC1_$AS_ID = 5;
    memset(FIM_$QUIT_EC, 0, sizeof(FIM_$QUIT_EC));
    memset(FIM_$QUIT_VALUE, 0, sizeof(FIM_$QUIT_VALUE));
    FIM_$QUIT_EC[5].value = 40;
    FIM_$QUIT_VALUE[5] = 40;
    ((ec_$eventcount_t *)(EC2_$WAIT_ECS + 3 * EC2_WAIT_EC_SIZE - EC2_WAIT_EC_SIZE))->value = 7;

    lock_calls = unlock_calls = lock_depth = 0;
    advance_calls = 0;
    fim_cleanup_result = status_$cleanup_handler_set;
    rls_cleanup_calls = fim_signal_calls = 0;
    waitn_calls = 0;
    waitn_result_i = 0;
    waitn_hook = NULL;
    waitn_watch = NULL;
    waitn_awaiters_seen = 0;
    memset(waitn_results, 0, sizeof(waitn_results));
}

static ec_$eventcount_t *wait_ec_of(uint16_t pid)
{
    return (ec_$eventcount_t *)(EC2_$WAIT_ECS + pid * EC2_WAIT_EC_SIZE - EC2_WAIT_EC_SIZE);
}

/* ==========================================================================
 * EC2_$WAIT (0x00E42358)
 * ========================================================================== */

TEST(wait_rejects_more_than_0x80_entries)
{
    ec2_$eventcount_t *ecs[1] = { NULL };
    int32_t vals[1] = { 0 };
    int16_t count = 0x81;
    status_$t status = -1;
    reset();
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 0);
    ASSERT_EQ(status, status_$ec2_internal_table_exhausted);
    ASSERT_EQ(lock_calls, 0);
    ASSERT_EQ(waitn_calls, 0);
}

TEST(wait_already_satisfied_address_handle_does_not_block)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 5 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ec_a->value = 5;
    ecs[0] = ec_a;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(waitn_calls, 0);
    /* the waiter record went back: ring empty, free list whole again */
    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(DAT_00e7cf08, 1);
    ASSERT_EQ(free_list_length(), 225);
    ASSERT_EQ(EC2_WAITER_TABLE_BASE[1].proc_id, 0);
    ASSERT_EQ(lock_calls, 1);
    ASSERT_EQ(unlock_calls, 1);
    ASSERT_EQ(lock_depth, 0);
    ASSERT_EQ(rls_cleanup_calls, 1);
}

static void hook_bump_ec_a(void)
{
    ec_a->value++;
}

TEST(wait_blocks_then_returns_the_satisfied_entry)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 11 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ec_a->value = 10;
    ecs[0] = ec_a;
    waitn_results[0] = 1;       /* the per-process wait eventcount fired */
    waitn_hook = hook_bump_ec_a;
    waitn_watch = ec_a;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(waitn_calls, 1);

    /* what EC_$WAITN was handed: the wait EC of PROC1_$CURRENT and the
     * quit EC of PROC1_$AS_ID, both waited for value + 1 */
    ASSERT_EQ(waitn_n, 2);
    ASSERT_TRUE(waitn_ecs[0] == wait_ec_of(3));
    ASSERT_EQ(waitn_vals[0], 8);
    ASSERT_TRUE(waitn_ecs[1] == &FIM_$QUIT_EC[5]);
    ASSERT_EQ(waitn_vals[1], 41);
    ASSERT_EQ(waitn_lock_depth, 0);

    /* during the wait, record 1 was threaded as a ring of one */
    ASSERT_EQ(waitn_awaiters_seen, 1);
    ASSERT_EQ(waitn_record_seen.next, 1);
    ASSERT_EQ(waitn_record_seen.prev, 1);
    ASSERT_EQ(waitn_record_seen.proc_id, 3);
    ASSERT_EQ(waitn_record_seen.wait_val, 11);

    /* afterwards everything is unthreaded again */
    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(free_list_length(), 225);
    ASSERT_EQ(lock_depth, 0);
    ASSERT_EQ(lock_calls, 2);
    ASSERT_EQ(unlock_calls, 2);
}

TEST(wait_loops_until_something_is_satisfied)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 12 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ec_a->value = 10;
    ecs[0] = ec_a;
    waitn_results[0] = 1;
    waitn_results[1] = 1;
    waitn_hook = hook_bump_ec_a;   /* 11 after the first round, 12 after the second */

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(waitn_calls, 2);
    ASSERT_EQ(ec_a->value, 12);
    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(free_list_length(), 225);
}

TEST(wait_quit_returns_zero_with_async_fault)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 100 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ec_a->value = 10;
    ecs[0] = ec_a;
    FIM_$QUIT_EC[5].value = 41;    /* a quit was advanced */
    waitn_results[0] = 2;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 0);
    ASSERT_EQ(status, status_$ec2_async_fault_while_waiting);
    ASSERT_EQ(FIM_$QUIT_VALUE[5], 41);
    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(free_list_length(), 225);
    ASSERT_EQ(rls_cleanup_calls, 1);
}

static void hook_bump_reg(void)
{
    reg_ec.value += 5;
}

TEST(wait_registered_index_goes_on_the_ec1_list)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 3 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ecs[0] = (ec2_$eventcount_t *)ARCH_VA_TO_PTR(2);
    waitn_results[0] = 3;
    waitn_hook = hook_bump_reg;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(waitn_n, 3);
    ASSERT_TRUE(waitn_ecs[2] == &reg_ec);
    ASSERT_EQ(waitn_vals[2], 3);
    ASSERT_EQ(free_list_length(), 225);
}

static void hook_bump_pbu0(void)
{
    ((ec_$eventcount_t *)EC2_$PBU_ECS)->value = 9;
}

TEST(wait_pbu_index_counts_references_around_the_wait)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 9 };
    int16_t count = 1;
    status_$t status = -1;
    int16_t *refcount = (int16_t *)(EC2_$PBU_ECS + 0x0E);
    reset();
    DAT_00e7cf00 = 0x1;     /* slot 0 allocated */
    *refcount = 0;
    ecs[0] = (ec2_$eventcount_t *)ARCH_VA_TO_PTR(0x101);
    waitn_results[0] = 3;
    waitn_hook = hook_bump_pbu0;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ok);
    ASSERT_TRUE(waitn_ecs[2] == (ec_$eventcount_t *)EC2_$PBU_ECS);
    ASSERT_EQ(*refcount, 0);      /* ++ before the wait, -- after */

    /* an unallocated slot is a bad eventcount, nothing waits */
    ecs[0] = (ec2_$eventcount_t *)ARCH_VA_TO_PTR(0x102);
    waitn_calls = 0;
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
    ASSERT_EQ(waitn_calls, 0);
}

TEST(wait_index_zero_and_one)
{
    ec2_$eventcount_t *ecs[2];
    int32_t vals[2] = { 0, 0 };
    int16_t count = 2;
    status_$t status;
    reset();

    /* index 0 is a bad eventcount; entry 1 is fine so it is the answer
     * after the cleanup pass re-tests it (value 0 - wait 0 >= 0) */
    ecs[0] = (ec2_$eventcount_t *)ARCH_VA_TO_PTR(2);
    ecs[1] = (ec2_$eventcount_t *)ARCH_VA_TO_PTR(0);
    status = -1;
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
    ASSERT_EQ(waitn_calls, 0);

    /* index 1 stops the scan at once with the status left alone */
    ecs[0] = ec_a;
    ec_a->value = -1;
    vals[0] = 0;
    ecs[1] = (ec2_$eventcount_t *)ARCH_VA_TO_PTR(1);
    status = -1;
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 2);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(waitn_calls, 0);
    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(free_list_length(), 225);
}

TEST(wait_lowest_satisfied_entry_wins)
{
    ec2_$eventcount_t *ecs[2];
    int32_t vals[2] = { 1, 1 };
    int16_t count = 2;
    status_$t status = -1;
    reset();
    ec_a->value = 0;
    ec_b->value = 5;    /* already satisfied: the scan stops at entry 2 */
    ecs[0] = ec_a;
    ecs[1] = ec_b;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 2);
    ASSERT_EQ(waitn_calls, 0);

    /* now both satisfied: the cleanup pass runs down to entry 1 */
    ec_a->value = 5;
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(ec_b->awaiters, 0);
    ASSERT_EQ(free_list_length(), 225);
}

static void hook_bump_ec_a_twice(void)
{
    ec_a->value += 2;
}

TEST(wait_two_records_on_one_eventcount_form_a_ring)
{
    ec2_$eventcount_t *ecs[2];
    int32_t vals[2] = { 12, 11 };
    int16_t count = 2;
    status_$t status = -1;
    reset();
    ec_a->value = 10;
    ecs[0] = ec_a;
    ecs[1] = ec_a;
    waitn_results[0] = 1;
    waitn_hook = hook_bump_ec_a_twice;
    waitn_watch = ec_a;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ok);

    /* record 1 was the head, record 2 inserted after it: 1 <-> 2 ring */
    ASSERT_EQ(waitn_awaiters_seen, 1);
    ASSERT_EQ(waitn_record_seen.next, 2);
    ASSERT_EQ(waitn_record_seen.prev, 2);

    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(free_list_length(), 225);
    ASSERT_EQ(EC2_WAITER_TABLE_BASE[1].proc_id, 0);
    ASSERT_EQ(EC2_WAITER_TABLE_BASE[2].proc_id, 0);
}

TEST(wait_empty_free_list_restores_the_waiter_word)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 1 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ec_a->value = 0;
    ec_a->awaiters = 0;
    ecs[0] = ec_a;
    DAT_00e7cf08 = 0;

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ec2_internal_table_exhausted);
    ASSERT_EQ(ec_a->awaiters, 0);
    ASSERT_EQ(waitn_calls, 0);
    ASSERT_EQ(lock_depth, 0);
}

TEST(wait_address_at_or_above_as_protection_is_a_violation)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 1 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ecs[0] = (ec2_$eventcount_t *)(arena + 0x8000);

    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$fault_protection_boundary_violation);
    ASSERT_EQ(waitn_calls, 0);
    ASSERT_EQ(free_list_length(), 225);
}

TEST(wait_stale_head_is_a_bad_eventcount)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 1 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ec_a->value = 0;
    ec_a->awaiters = 0xF0;      /* above 0xE0 */
    ecs[0] = ec_a;
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
    ASSERT_EQ(ec_a->awaiters, 0xF0);
    ASSERT_EQ(free_list_length(), 225);

    ec_a->awaiters = 7;         /* a free record */
    status = -1;
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 1);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
    ASSERT_EQ(ec_a->awaiters, 7);
    ASSERT_EQ(free_list_length(), 225);
}

TEST(wait_full_ec1_list_is_table_exhausted)
{
    ec2_$eventcount_t *ecs[0x20];
    int32_t vals[0x20];
    int16_t count = 0x1F;
    status_$t status = -1;
    int k;
    reset();
    reg_ec.value = -1;
    for (k = 0; k < 0x20; k++) {
        ecs[k] = (ec2_$eventcount_t *)ARCH_VA_TO_PTR(2);
        vals[k] = 0;
    }
    /* 30 registered entries fit (slots 2..31); the 31st does not */
    ASSERT_EQ(EC2_$WAIT(ecs, vals, &count, &status), 0x1F);
    ASSERT_EQ(status, status_$ec2_internal_table_exhausted);
    ASSERT_EQ(waitn_calls, 0);
}

TEST(wait_fault_unwind_returns_records_and_resignals)
{
    ec2_$eventcount_t *ecs[1];
    int32_t vals[1] = { 1 };
    int16_t count = 1;
    status_$t status = -1;
    reset();
    ecs[0] = ec_a;
    fim_cleanup_result = 0x12345;

    EC2_$WAIT(ecs, vals, &count, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(fim_signal_calls, 1);
    ASSERT_EQ(fim_signal_status, 0x12345);
    ASSERT_EQ(unlock_calls, 1);
    ASSERT_EQ(lock_calls, 0);
    ASSERT_EQ(waitn_calls, 0);
}

/* ==========================================================================
 * EC2_$WAKEUP (0x00E4285A)
 * ========================================================================== */

TEST(wakeup_advances_only_satisfied_waiters)
{
    status_$t status = -1;
    reset();
    /* ring 4 <-> 9: pid 3 waits for 10, pid 4 waits for 20 */
    EC2_WAITER_TABLE_BASE[4].next = 9;
    EC2_WAITER_TABLE_BASE[4].prev = 9;
    EC2_WAITER_TABLE_BASE[4].proc_id = 3;
    EC2_WAITER_TABLE_BASE[4].wait_val = 10;
    EC2_WAITER_TABLE_BASE[9].next = 4;
    EC2_WAITER_TABLE_BASE[9].prev = 4;
    EC2_WAITER_TABLE_BASE[9].proc_id = 4;
    EC2_WAITER_TABLE_BASE[9].wait_val = 20;
    ec_a->value = 15;
    ec_a->awaiters = 4;

    EC2_$WAKEUP(ec_a, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(advance_calls, 1);
    ASSERT_TRUE(advance_ecs[0] == wait_ec_of(3));
    ASSERT_EQ(lock_calls, 1);
    ASSERT_EQ(unlock_calls, 1);
    ASSERT_EQ(rls_cleanup_calls, 1);

    ec_a->value = 20;
    advance_calls = 0;
    EC2_$WAKEUP(ec_a, &status);
    ASSERT_EQ(advance_calls, 2);
    ASSERT_TRUE(advance_ecs[0] == wait_ec_of(3));
    ASSERT_TRUE(advance_ecs[1] == wait_ec_of(4));
}

TEST(wakeup_no_waiters_and_bad_heads)
{
    status_$t status = -1;
    reset();
    ec_a->awaiters = 0;
    EC2_$WAKEUP(ec_a, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(advance_calls, 0);

    ec_a->awaiters = 0xE1;
    EC2_$WAKEUP(ec_a, &status);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    ec_a->awaiters = 0xE0;      /* in range, but a free record */
    status = -1;
    EC2_$WAKEUP(ec_a, &status);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
    ASSERT_EQ(advance_calls, 0);
    ASSERT_EQ(lock_depth, 0);
}

TEST(wakeup_fault_unwind_resignals)
{
    status_$t status = -1;
    reset();
    fim_cleanup_result = 0x777;
    EC2_$WAKEUP(ec_a, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(fim_signal_calls, 1);
    ASSERT_EQ(fim_signal_status, 0x777);
    ASSERT_EQ(unlock_calls, 1);
    ASSERT_EQ(rls_cleanup_calls, 0);
}

/* ==========================================================================
 * main
 * ========================================================================== */

int main(void)
{
    printf("EC2_$WAIT / EC2_$WAKEUP tests\n");

    RUN_TEST(wait_rejects_more_than_0x80_entries);
    RUN_TEST(wait_already_satisfied_address_handle_does_not_block);
    RUN_TEST(wait_blocks_then_returns_the_satisfied_entry);
    RUN_TEST(wait_loops_until_something_is_satisfied);
    RUN_TEST(wait_quit_returns_zero_with_async_fault);
    RUN_TEST(wait_registered_index_goes_on_the_ec1_list);
    RUN_TEST(wait_pbu_index_counts_references_around_the_wait);
    RUN_TEST(wait_index_zero_and_one);
    RUN_TEST(wait_lowest_satisfied_entry_wins);
    RUN_TEST(wait_two_records_on_one_eventcount_form_a_ring);
    RUN_TEST(wait_empty_free_list_restores_the_waiter_word);
    RUN_TEST(wait_address_at_or_above_as_protection_is_a_violation);
    RUN_TEST(wait_stale_head_is_a_bad_eventcount);
    RUN_TEST(wait_full_ec1_list_is_table_exhausted);
    RUN_TEST(wait_fault_unwind_returns_records_and_resignals);
    RUN_TEST(wakeup_advances_only_satisfied_waiters);
    RUN_TEST(wakeup_no_waiters_and_bad_heads);
    RUN_TEST(wakeup_fault_unwind_resignals);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
