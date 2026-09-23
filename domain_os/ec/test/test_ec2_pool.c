/*
 * ec/test/test_ec2_pool.c - Unit tests for the EC2 pool/registration entry
 * points re-emitted from 0x00E30970 and 0x00E42358..0x00E42CEC:
 *
 *   EC2_$INIT_S, EC2_$INIT, EC2_$READ, EC2_$GET_VAL, EC2_$GET_EC1_ADDR,
 *   EC2_$ALLOCATE_EC1, EC2_$RELEASE_EC1, EC2_$REGISTER_EC1, EC2_$ADVANCE
 *
 * The real ec/ec_data.c cells are used.  ML_$LOCK/ML_$UNLOCK, EC_$INIT and
 * EC2_$WAKEUP are mocked and record their calls.  EC_$INIT is mocked rather
 * than included because the host ec_$eventcount_t (two 64-bit pointers) is
 * wider than the 0x0C-byte records EC2_$INIT_S walks.
 */

#include "ec/ec_internal.h"
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

static int ec_init_calls;
static void *ec_init_first, *ec_init_last;

void EC_$INIT(ec_$eventcount_t *ec)
{
    /* Only the value longword is touched: the pool records are 0x0C / 0x18
     * bytes and the host structure is wider. */
    ec->value = 0;
    if (ec_init_calls == 0) {
        ec_init_first = ec;
    }
    ec_init_last = ec;
    ec_init_calls++;
}

static int advance_all_calls;
static ec_$eventcount_t *advance_all_ec;

void EC_$ADVANCE_ALL(ec_$eventcount_t *ec)
{
    advance_all_calls++;
    advance_all_ec = ec;
}

static int wakeup_calls;
static ec2_$eventcount_t *wakeup_ec;
static status_$t *wakeup_status;

void EC2_$WAKEUP(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    wakeup_calls++;
    wakeup_ec = ec;
    wakeup_status = status_ret;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../ec_data.c"
#include "../ec2_init_s.c"
#include "../ec2_init.c"
#include "../ec2_read.c"
#include "../ec2_get_val.c"
#include "../ec2_get_ec1_addr.c"
#include "../ec2_allocate_ec1.c"
#include "../ec2_release_ec1.c"
#include "../ec2_register_ec1.c"
#include "../ec2_advance.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void reset(void)
{
    memset(EC2_$WAIT_ECS, 0x5A, sizeof(EC2_$WAIT_ECS));
    memset(EC2_WAITER_TABLE_BASE, 0x5A, sizeof(EC2_WAITER_TABLE_BASE));
    memset(DAT_00e7caf8, 0, sizeof(DAT_00e7caf8));
    memset(EC2_$PBU_ECS, 0, sizeof(EC2_$PBU_ECS));
    DAT_00e7caf0 = 0x1234;
    DAT_00e7cefc = 0xFFFFFFFF;
    DAT_00e7cf00 = 0xFFFFFFFF;
    _DAT_00e7cf04 = 99;
    DAT_00e7cf06 = 0;
    DAT_00e7cf08 = 0x77;
    lock_calls = unlock_calls = lock_depth = 0;
    ec_init_calls = 0;
    ec_init_first = ec_init_last = NULL;
    advance_all_calls = 0;
    advance_all_ec = NULL;
    wakeup_calls = 0;
    wakeup_ec = NULL;
    wakeup_status = NULL;
    EC2_$INIT_S();
    ec_init_calls = 0;
}

static int16_t *pbu_refcount(int slot)
{
    return (int16_t *)(EC2_$PBU_ECS + slot * EC2_PBU_EC_SIZE + 0x0E);
}

static ec_$eventcount_t *pbu_ec(int slot)
{
    return (ec_$eventcount_t *)(EC2_$PBU_ECS + slot * EC2_PBU_EC_SIZE);
}

/* ==========================================================================
 * EC2_$INIT_S (0x00E30970)
 * ========================================================================== */

TEST(init_s_inits_64_wait_ecs_at_stride_0c)
{
    reset();
    ec_init_calls = 0;
    ec_init_first = ec_init_last = NULL;
    EC2_$INIT_S();
    ASSERT_EQ(ec_init_calls, 64);
    ASSERT_TRUE(ec_init_first == (void *)EC2_$WAIT_ECS);
    ASSERT_TRUE(ec_init_last == (void *)(EC2_$WAIT_ECS + 63 * EC2_WAIT_EC_SIZE));
}

TEST(init_s_threads_225_waiters_onto_free_list)
{
    reset();
    for (int i = 0; i < EC2_WAITER_TABLE_SLOTS; i++) {
        ASSERT_EQ(EC2_WAITER_TABLE_BASE[i].wait_val, 0);
        ASSERT_EQ(EC2_WAITER_TABLE_BASE[i].proc_id, 0);
        ASSERT_EQ((uint16_t)EC2_WAITER_TABLE_BASE[i].next, (uint16_t)(i + 1));
        /* prev and pad are not written: they keep the fill pattern */
        ASSERT_EQ((uint16_t)EC2_WAITER_TABLE_BASE[i].prev, 0x5A5A);
    }
}

TEST(init_s_resets_module_cells)
{
    reset();
    ASSERT_EQ(DAT_00e7caf0, 0);
    ASSERT_EQ(DAT_00e7cf08, 1);
    ASSERT_EQ(_DAT_00e7cf04, 1);
    ASSERT_TRUE(DAT_00e7cafc == (void *)&_DAT_00e7cf04);
    ASSERT_TRUE(DAT_00e7caf8[1] == (void *)&_DAT_00e7cf04);
    ASSERT_EQ(DAT_00e7cf00, 0);
    ASSERT_EQ(DAT_00e7cefc, 0);
    /* not written by INIT_S */
    ASSERT_EQ(DAT_00e7cf06, 0);
}

/* ==========================================================================
 * EC2_$INIT (0x00E42C60)
 * ========================================================================== */

TEST(init_zeroes_a_real_eventcount)
{
    ec2_$eventcount_t ec = { 0x12345678, 0x55 };
    EC2_$INIT(&ec);
    ASSERT_EQ(ec.value, 0);
    ASSERT_EQ(ec.awaiters, 0);
}

TEST(init_ignores_an_index)
{
    /* 0x3E8 itself is "not above", so it is treated as an index too */
    EC2_$INIT((ec2_$eventcount_t *)ARCH_VA_TO_PTR(0x3E8));
    EC2_$INIT((ec2_$eventcount_t *)ARCH_VA_TO_PTR(0x105));
    ASSERT_TRUE(1);
}

/* ==========================================================================
 * EC2_$REGISTER_EC1 (0x00E4293C)
 * ========================================================================== */

TEST(register_appends_after_the_count_slot)
{
    ec_$eventcount_t a, b;
    status_$t status = -1;
    reset();
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$REGISTER_EC1(&a, &status)), 2);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(_DAT_00e7cf04, 2);
    ASSERT_TRUE(DAT_00e7caf8[2] == (void *)&a);
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$REGISTER_EC1(&b, &status)), 3);
    ASSERT_EQ(_DAT_00e7cf04, 3);
    ASSERT_TRUE(DAT_00e7caf8[3] == (void *)&b);
    ASSERT_EQ(lock_calls, 2);
    ASSERT_EQ(unlock_calls, 2);
    ASSERT_EQ(lock_depth, 0);
    ASSERT_EQ(last_lock_id, EC2_LOCK_ID);
}

TEST(register_scans_only_dat_00e7cf06_slots_for_a_duplicate)
{
    ec_$eventcount_t a, b;
    status_$t status;
    reset();
    EC2_$REGISTER_EC1(&a, &status);   /* 2 */
    EC2_$REGISTER_EC1(&b, &status);   /* 3 */

    /* scan bound 0: no scan, so a duplicate is appended */
    DAT_00e7cf06 = 0;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$REGISTER_EC1(&a, &status)), 4);

    /* scan bound 3 covers slots 1..3: slot 2 already holds a */
    DAT_00e7cf06 = 3;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$REGISTER_EC1(&a, &status)), 2);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(_DAT_00e7cf04, 4);

    /* scan bound 2 covers slots 1..2 only: b (slot 3) is not seen */
    DAT_00e7cf06 = 2;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$REGISTER_EC1(&b, &status)), 5);
    ASSERT_EQ(lock_depth, 0);
}

TEST(register_refuses_when_table_is_at_0x100)
{
    ec_$eventcount_t a;
    status_$t status = -1;
    reset();
    _DAT_00e7cf04 = 0x100;
    ASSERT_TRUE(EC2_$REGISTER_EC1(&a, &status) == NULL);
    ASSERT_EQ(status, status_$ec2_internal_table_exhausted);
    ASSERT_EQ(_DAT_00e7cf04, 0x100);
    ASSERT_EQ(lock_depth, 0);

    /* 0xFF is still below 0x100 and gets slot 0x100 */
    _DAT_00e7cf04 = 0xFF;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$REGISTER_EC1(&a, &status)), 0x100);
    ASSERT_EQ(status, status_$ok);
    ASSERT_TRUE(DAT_00e7caf8[0x100] == (void *)&a);
}

/* ==========================================================================
 * EC2_$ALLOCATE_EC1 (0x00E429DA)
 * ========================================================================== */

TEST(allocate_hands_out_free_slots_in_order)
{
    status_$t status = -1;
    reset();
    *pbu_refcount(0) = 0x1111;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$ALLOCATE_EC1(&status)), 0x101);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(DAT_00e7cf00, 0x1);
    ASSERT_EQ(*pbu_refcount(0), 0);
    ASSERT_EQ(ec_init_calls, 1);
    ASSERT_TRUE(ec_init_last == (void *)pbu_ec(0));

    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$ALLOCATE_EC1(&status)), 0x102);
    ASSERT_EQ(DAT_00e7cf00, 0x3);
    ASSERT_EQ(lock_calls, 2);
    ASSERT_EQ(unlock_calls, 2);
    ASSERT_EQ(lock_depth, 0);
    ASSERT_EQ(last_lock_id, EC2_LOCK_ID);
}

TEST(allocate_skips_allocated_bits)
{
    status_$t status;
    reset();
    DAT_00e7cf00 = 0x0000000F;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$ALLOCATE_EC1(&status)), 0x105);
    ASSERT_EQ(DAT_00e7cf00, 0x1F);
    DAT_00e7cf00 = 0x7FFFFFFF;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$ALLOCATE_EC1(&status)), 0x120);
    ASSERT_EQ(DAT_00e7cf00, 0xFFFFFFFF);
}

TEST(allocate_reclaims_a_pending_release_with_no_waiters)
{
    status_$t status;
    reset();
    DAT_00e7cf00 = 0x00000007;   /* slots 0..2 allocated       */
    DAT_00e7cefc = 0x00000002;   /* slot 1 pending release     */
    *pbu_refcount(1) = 0;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$ALLOCATE_EC1(&status)), 0x102);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(DAT_00e7cf00, 0x7);
    ASSERT_EQ(DAT_00e7cefc, 0x0);
    ASSERT_TRUE(ec_init_last == (void *)pbu_ec(1));
}

TEST(allocate_leaves_a_pending_release_with_waiters)
{
    status_$t status;
    reset();
    DAT_00e7cf00 = 0x00000007;
    DAT_00e7cefc = 0x00000002;
    *pbu_refcount(1) = 1;
    ASSERT_EQ(ARCH_PTR_TO_VA(EC2_$ALLOCATE_EC1(&status)), 0x104);
    ASSERT_EQ(DAT_00e7cf00, 0xF);
    ASSERT_EQ(DAT_00e7cefc, 0x2);
    ASSERT_EQ(*pbu_refcount(1), 1);
}

TEST(allocate_fails_when_all_32_are_in_use)
{
    status_$t status = -1;
    reset();
    DAT_00e7cf00 = 0xFFFFFFFF;
    ASSERT_TRUE(EC2_$ALLOCATE_EC1(&status) == NULL);
    ASSERT_EQ(status, status_$ec2_unable_to_allocate_level_1_eventcount);
    ASSERT_EQ(ec_init_calls, 0);
    ASSERT_EQ(lock_depth, 0);
}

/* ==========================================================================
 * EC2_$RELEASE_EC1 (0x00E42B32)
 * ========================================================================== */

TEST(release_frees_a_slot_with_no_waiters)
{
    ec2_$eventcount_t idx;
    status_$t status = -1;
    reset();
    EC2_$ALLOCATE_EC1(&status);               /* 0x101 */
    EC2_$ALLOCATE_EC1(&status);               /* 0x102 */
    lock_calls = unlock_calls = 0;

    idx.value = 0x101;
    EC2_$RELEASE_EC1(&idx, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(DAT_00e7cf00, 0x2);
    ASSERT_EQ(DAT_00e7cefc, 0x0);
    ASSERT_EQ(advance_all_calls, 0);
    ASSERT_EQ(lock_calls, 1);
    ASSERT_EQ(unlock_calls, 1);
    ASSERT_EQ(last_lock_id, EC2_LOCK_ID);
}

TEST(release_defers_a_slot_with_waiters)
{
    ec2_$eventcount_t idx;
    status_$t status = -1;
    reset();
    EC2_$ALLOCATE_EC1(&status);               /* 0x101 */
    *pbu_refcount(0) = 2;

    idx.value = 0x101;
    EC2_$RELEASE_EC1(&idx, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(DAT_00e7cf00, 0x1);            /* still allocated */
    ASSERT_EQ(DAT_00e7cefc, 0x1);            /* pending release */
    ASSERT_EQ(advance_all_calls, 1);
    ASSERT_TRUE(advance_all_ec == pbu_ec(0));
    ASSERT_EQ(*pbu_refcount(0), 2);          /* not touched here */
}

TEST(release_rejects_bad_and_unallocated)
{
    ec2_$eventcount_t idx;
    status_$t status;
    reset();

    idx.value = 0x100;
    status = -1;
    EC2_$RELEASE_EC1(&idx, &status);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    idx.value = 0x121;
    EC2_$RELEASE_EC1(&idx, &status);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    idx.value = 0x105;
    EC2_$RELEASE_EC1(&idx, &status);
    ASSERT_EQ(status, status_$ec2_level_1_ec_not_allocated);
    ASSERT_EQ(advance_all_calls, 0);
    ASSERT_EQ(lock_depth, 0);
}

/* ==========================================================================
 * EC2_$GET_VAL (0x00E42BE0)
 * ========================================================================== */

TEST(get_val_registered_and_count_cell)
{
    ec_$eventcount_t a;
    ec2_$eventcount_t idx;
    status_$t status;
    reset();
    a.value = 0x0BADF00D;
    EC2_$REGISTER_EC1(&a, &status);           /* index 2 */

    idx.value = 2;
    status = -1;
    ASSERT_EQ(EC2_$GET_VAL(&idx, &status), 0x0BADF00D);
    ASSERT_EQ(status, status_$ok);

    /* index 1 is the registration count cell itself */
    idx.value = 1;
    ASSERT_EQ(EC2_$GET_VAL(&idx, &status), 2);
    ASSERT_EQ(status, status_$ok);

    /* beyond the high-water mark */
    idx.value = 3;
    ASSERT_EQ((uint32_t)EC2_$GET_VAL(&idx, &status), 0x7FFFFFFF);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    idx.value = 0;
    ASSERT_EQ((uint32_t)EC2_$GET_VAL(&idx, &status), 0x7FFFFFFF);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
    ASSERT_EQ(lock_calls, 1);   /* only the register call took the lock */
}

TEST(get_val_pbu_pool)
{
    ec2_$eventcount_t idx;
    status_$t status;
    reset();
    EC2_$ALLOCATE_EC1(&status);               /* 0x101 */
    pbu_ec(0)->value = 42;

    idx.value = 0x101;
    ASSERT_EQ(EC2_$GET_VAL(&idx, &status), 42);
    ASSERT_EQ(status, status_$ok);

    idx.value = 0x102;
    ASSERT_EQ((uint32_t)EC2_$GET_VAL(&idx, &status), 0x7FFFFFFF);
    ASSERT_EQ(status, status_$ec2_level_1_ec_not_allocated);

    idx.value = 0x121;
    ASSERT_EQ((uint32_t)EC2_$GET_VAL(&idx, &status), 0x7FFFFFFF);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    idx.value = 0x100;
    ASSERT_EQ((uint32_t)EC2_$GET_VAL(&idx, &status), 0x7FFFFFFF);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
}

/* ==========================================================================
 * EC2_$GET_EC1_ADDR (0x00E42A8A)
 * ========================================================================== */

TEST(get_ec1_addr_resolves_registered_and_pool)
{
    ec_$eventcount_t a;
    ec2_$eventcount_t idx;
    status_$t status;
    reset();
    EC2_$REGISTER_EC1(&a, &status);           /* 2 */
    EC2_$ALLOCATE_EC1(&status);               /* 0x101 */
    EC2_$ALLOCATE_EC1(&status);               /* 0x102 */
    lock_calls = unlock_calls = 0;

    idx.value = 2;
    status = -1;
    ASSERT_TRUE(EC2_$GET_EC1_ADDR(&idx, &status) == &a);
    ASSERT_EQ(status, status_$ok);

    idx.value = 0x102;
    ASSERT_TRUE(EC2_$GET_EC1_ADDR(&idx, &status) == pbu_ec(1));
    ASSERT_EQ(status, status_$ok);

    /* unlike GET_VAL, index 1 is rejected here */
    idx.value = 1;
    ASSERT_TRUE(EC2_$GET_EC1_ADDR(&idx, &status) == NULL);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    idx.value = 0x103;
    ASSERT_TRUE(EC2_$GET_EC1_ADDR(&idx, &status) == NULL);
    ASSERT_EQ(status, status_$ec2_level_1_ec_not_allocated);

    idx.value = 0x121;
    ASSERT_TRUE(EC2_$GET_EC1_ADDR(&idx, &status) == NULL);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    idx.value = 3;
    ASSERT_TRUE(EC2_$GET_EC1_ADDR(&idx, &status) == NULL);
    ASSERT_EQ(status, status_$ec2_bad_event_count);

    ASSERT_EQ(lock_calls, 6);
    ASSERT_EQ(unlock_calls, 6);
    ASSERT_EQ(lock_depth, 0);
}

/* ==========================================================================
 * EC2_$ADVANCE (0x00E42CAE)
 * ========================================================================== */

TEST(advance_bumps_and_wakes_only_with_waiters)
{
    ec2_$eventcount_t ec = { 10, 0 };
    status_$t status = -1;
    reset();
    EC2_$ADVANCE(&ec, &status);
    ASSERT_EQ(ec.value, 11);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(wakeup_calls, 0);

    ec.awaiters = 7;
    EC2_$ADVANCE(&ec, &status);
    ASSERT_EQ(ec.value, 12);
    ASSERT_EQ(wakeup_calls, 1);
    ASSERT_TRUE(wakeup_ec == &ec);
    ASSERT_TRUE(wakeup_status == &status);
}

TEST(advance_rejects_an_index)
{
    status_$t status = -1;
    reset();
    EC2_$ADVANCE((ec2_$eventcount_t *)ARCH_VA_TO_PTR(0x3E8), &status);
    ASSERT_EQ(status, status_$ec2_bad_event_count);
    ASSERT_EQ(wakeup_calls, 0);
}

/* ==========================================================================
 * EC2_$READ (0x00E42C7C)
 * ========================================================================== */

TEST(read_direct_and_via_index)
{
    ec_$eventcount_t a;
    ec2_$eventcount_t ec = { 0x600D, 0 };
    status_$t status;
    reset();
    a.value = 0x1234;
    EC2_$REGISTER_EC1(&a, &status);           /* 2 */

    ASSERT_EQ(EC2_$READ(&ec), 0x600D);
    ASSERT_EQ(EC2_$READ((ec2_$eventcount_t *)ARCH_VA_TO_PTR(2)), 0x1234);
    ASSERT_EQ((uint32_t)EC2_$READ((ec2_$eventcount_t *)ARCH_VA_TO_PTR(0x3E8)),
              0x7FFFFFFF);
}

/* ==========================================================================
 * main
 * ========================================================================== */

int main(void)
{
    printf("EC2 pool / registration tests\n");

    RUN_TEST(init_s_inits_64_wait_ecs_at_stride_0c);
    RUN_TEST(init_s_threads_225_waiters_onto_free_list);
    RUN_TEST(init_s_resets_module_cells);
    RUN_TEST(init_zeroes_a_real_eventcount);
    RUN_TEST(init_ignores_an_index);
    RUN_TEST(register_appends_after_the_count_slot);
    RUN_TEST(register_scans_only_dat_00e7cf06_slots_for_a_duplicate);
    RUN_TEST(register_refuses_when_table_is_at_0x100);
    RUN_TEST(allocate_hands_out_free_slots_in_order);
    RUN_TEST(allocate_skips_allocated_bits);
    RUN_TEST(allocate_reclaims_a_pending_release_with_no_waiters);
    RUN_TEST(allocate_leaves_a_pending_release_with_waiters);
    RUN_TEST(allocate_fails_when_all_32_are_in_use);
    RUN_TEST(release_frees_a_slot_with_no_waiters);
    RUN_TEST(release_defers_a_slot_with_waiters);
    RUN_TEST(release_rejects_bad_and_unallocated);
    RUN_TEST(get_val_registered_and_count_cell);
    RUN_TEST(get_val_pbu_pool);
    RUN_TEST(get_ec1_addr_resolves_registered_and_pool);
    RUN_TEST(advance_bumps_and_wakes_only_with_waiters);
    RUN_TEST(advance_rejects_an_index);
    RUN_TEST(read_direct_and_via_index);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
