/*
 * disk/test/test_rtn_qblks_internal.c - Unit tests for
 * disk_$rtn_qblks_internal (0x00E3C01A)
 *
 * The real disk/rtn_qblks_internal.c is #included below and driven through a
 * host copy of the disk module data area; ML_$EXCLUSION_START/STOP and
 * EC_$ADVANCE are stubbed and counted.
 *
 * Covered:
 *   - the reserve-block arm (first == DMOD_RESERVE_BLOCK): sets
 *     DMOD_RESERVE_AVAIL and advances the eventcount only when there are no
 *     pending requests (0x00E3C040-0x00E3C04C)
 *   - the free-list push: last->free_next = old head, head = first,
 *     avail += count (0x00E3C056-0x00E3C060)
 *   - the service loop wakes exactly the requests the free list can satisfy
 *     and stops at the first one that is too big (0x00E3C098-0x00E3C0AE)
 *   - the one-based request queue and its 0x40 -> 1 wrap (0x00E3C084)
 *   - the lock is taken and released on every path
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "arch/arch.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Module data and mocks
 * ============================================================================ */

/* Big enough to cover every offset the function touches (max 0xAFC). */
uint8_t DISK_$DATA[0xB00];

static int lock_starts;
static int lock_stops;
static int ec_advances;

void ML_$EXCLUSION_START(ml_$exclusion_t *excl) { (void)excl; lock_starts++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)  { (void)excl; lock_stops++; }
void EC_$ADVANCE(ec_$eventcount_t *ec)          { (void)ec; ec_advances++; }

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../rtn_qblks_internal.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

/*
 * The free-list links are 32-bit target VAs, so the blocks have to live in
 * an arena that ARCH_HOST_VA_BASE points at (see arch/host/arch.h).
 */
static uint8_t arena[0x100];

#define ARENA_BLOCK_A  0x20
#define ARENA_BLOCK_B  0x40
#define ARENA_RESERVE  0x60

#define block_a        (arena + ARENA_BLOCK_A)
#define block_b        (arena + ARENA_BLOCK_B)
#define reserve_block  (arena + ARENA_RESERVE)

#define D16(off)  (*(int16_t *)(DISK_$DATA + (off)))
#define D8(off)   (*(int8_t *)(DISK_$DATA + (off)))
#define DVA(off)  (*(uint32_t *)(DISK_$DATA + (off)))
#define REQ(i)    (((int16_t *)(DISK_$DATA + DMOD_REQ_QUEUE))[i])

static void reset_module(void)
{
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(arena, 0, sizeof(arena));

    lock_starts = 0;
    lock_stops = 0;
    ec_advances = 0;

    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    DVA(DMOD_RESERVE_BLOCK) = ARENA_RESERVE;
    D16(DMOD_REQ_READ_IDX) = 1;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(reserve_block_with_no_pending_advances_the_ec)
{
    reset_module();
    D16(DMOD_PENDING_COUNT) = 0;

    disk_$rtn_qblks_internal(1, reserve_block, reserve_block);

    ASSERT_EQ(-1, D8(DMOD_RESERVE_AVAIL));
    ASSERT_EQ(1, ec_advances);
    /* The free list is untouched on this arm. */
    ASSERT_EQ(0u, DVA(DMOD_FREE_HEAD));
    ASSERT_EQ(0, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(1, lock_starts);
    ASSERT_EQ(1, lock_stops);
}

TEST(reserve_block_with_pending_does_not_advance)
{
    reset_module();
    D16(DMOD_PENDING_COUNT) = 1;
    REQ(1) = 4;

    disk_$rtn_qblks_internal(1, reserve_block, reserve_block);

    ASSERT_EQ(-1, D8(DMOD_RESERVE_AVAIL));
    ASSERT_EQ(0, ec_advances);
    /* The pending request is left queued: this arm never serves it. */
    ASSERT_EQ(1, D16(DMOD_PENDING_COUNT));
    ASSERT_EQ(1, lock_stops);
}

TEST(free_list_push_links_last_to_the_old_head)
{
    reset_module();
    DVA(DMOD_FREE_HEAD) = ARENA_BLOCK_B;
    D16(DMOD_AVAIL_COUNT) = 3;

    disk_$rtn_qblks_internal(2, block_a, block_a);

    ASSERT_EQ((uint32_t)ARENA_BLOCK_B,
              *(uint32_t *)(block_a + DISK_QBLK_FREE_NEXT));
    ASSERT_EQ((uint32_t)ARENA_BLOCK_A, DVA(DMOD_FREE_HEAD));
    ASSERT_EQ(5, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(0, ec_advances);
    /* The reserve flag belongs to the other arm and must stay clear. */
    ASSERT_EQ(0, D8(DMOD_RESERVE_AVAIL));
}

TEST(first_and_last_can_differ)
{
    reset_module();
    DVA(DMOD_FREE_HEAD) = 0;

    disk_$rtn_qblks_internal(2, block_a, block_b);

    /* Only the LAST block's free_next is rewritten. */
    ASSERT_EQ(0u, *(uint32_t *)(block_b + DISK_QBLK_FREE_NEXT));
    ASSERT_EQ(0u, *(uint32_t *)(block_a + DISK_QBLK_FREE_NEXT));
    ASSERT_EQ((uint32_t)ARENA_BLOCK_A, DVA(DMOD_FREE_HEAD));
}

TEST(service_loop_wakes_what_it_can_and_stops)
{
    reset_module();
    D16(DMOD_PENDING_COUNT) = 3;
    D16(DMOD_REQ_READ_IDX) = 1;
    REQ(1) = 2;
    REQ(2) = 3;
    REQ(3) = 9;         /* larger than what is left: the loop stops here */

    disk_$rtn_qblks_internal(6, block_a, block_a);

    ASSERT_EQ(2, ec_advances);
    ASSERT_EQ(1, D16(DMOD_PENDING_COUNT));
    ASSERT_EQ(1, D16(DMOD_AVAIL_COUNT));    /* 6 - 2 - 3 */
    ASSERT_EQ(3, D16(DMOD_REQ_READ_IDX));
    ASSERT_EQ(1, lock_stops);
}

TEST(service_loop_is_not_entered_without_pending_requests)
{
    reset_module();
    D16(DMOD_PENDING_COUNT) = 0;
    REQ(1) = 1;

    disk_$rtn_qblks_internal(4, block_a, block_a);

    ASSERT_EQ(0, ec_advances);
    ASSERT_EQ(4, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(1, D16(DMOD_REQ_READ_IDX));
}

TEST(read_index_wraps_from_0x40_to_1)
{
    reset_module();
    D16(DMOD_PENDING_COUNT) = 1;
    D16(DMOD_REQ_READ_IDX) = DMOD_REQ_QUEUE_SIZE;   /* 0x40 */
    REQ(DMOD_REQ_QUEUE_SIZE) = 1;

    disk_$rtn_qblks_internal(1, block_a, block_a);

    ASSERT_EQ(1, ec_advances);
    ASSERT_EQ(1, D16(DMOD_REQ_READ_IDX));
    ASSERT_EQ(0, D16(DMOD_PENDING_COUNT));
}

TEST(a_request_of_exactly_the_available_count_is_served)
{
    reset_module();
    D16(DMOD_PENDING_COUNT) = 1;
    D16(DMOD_REQ_READ_IDX) = 1;
    REQ(1) = 5;

    disk_$rtn_qblks_internal(5, block_a, block_a);

    /* cmp.w (0xaf8,A5),D1w / ble: equal is served. */
    ASSERT_EQ(1, ec_advances);
    ASSERT_EQ(0, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(2, D16(DMOD_REQ_READ_IDX));
}

int main(void)
{
    printf("test_rtn_qblks_internal:\n");

    RUN_TEST(reserve_block_with_no_pending_advances_the_ec);
    RUN_TEST(reserve_block_with_pending_does_not_advance);
    RUN_TEST(free_list_push_links_last_to_the_old_head);
    RUN_TEST(first_and_last_can_differ);
    RUN_TEST(service_loop_wakes_what_it_can_and_stops);
    RUN_TEST(service_loop_is_not_entered_without_pending_requests);
    RUN_TEST(read_index_wraps_from_0x40_to_1);
    RUN_TEST(a_request_of_exactly_the_available_count_is_served);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
