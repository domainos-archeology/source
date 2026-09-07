/*
 * disk/test/test_get_qblks_internal.c - Unit tests for
 * disk_$get_qblks_internal (0x00E3BE8A)
 *
 * The real disk/get_qblks_internal.c is #included below and driven through a
 * host copy of the disk module data area; ML_$EXCLUSION_START/STOP, EC_$WAIT
 * and disk_$grow_qblk_pool are stubbed and counted.
 *
 * The free-list cells (DMOD_RESERVE_BLOCK 0x0BC, DMOD_FREE_HEAD 0x0C0,
 * DISK_QBLK_FORWARD 0x00, DISK_QBLK_FREE_NEXT 0x08) hold 32-bit target
 * virtual addresses, so the queue blocks live in an arena that
 * ARCH_HOST_VA_BASE points at and the cells hold offsets into it.
 *
 * Covered:
 *   - the fast path: count <= avail with no pending requests, avail
 *     decremented by count (0x00E3BEB8-0x00E3BECC)
 *   - block initialisation and the allocated chain built through
 *     DISK_QBLK_FORWARD (0x00E3BF90-0x00E3BFD0)
 *   - the last block's free_next and forward links are cleared, in that
 *     order (0x00E3BFDA, 0x00E3BFDE)
 *   - write mode takes the reserve block by copying DMOD_RESERVE_BLOCK into
 *     DMOD_FREE_HEAD (0x00E3BF02) and clearing DMOD_RESERVE_AVAIL
 *   - write mode ignores the pending count; read mode does not
 *     (0x00E3BEBE-0x00E3BEC6)
 *   - read mode enqueues into the one-based request queue and wraps the
 *     write index from 0x40 to 1 (0x00E3BF14-0x00E3BF34)
 *   - the pool is grown only when growth is enabled, nothing is pending and
 *     PROC1_$TYPE[PROC1_$CURRENT] != 5 (0x00E3BED0-0x00E3BEF6)
 *   - the lock is taken and released around the whole body, and dropped
 *     across EC_$WAIT (0x00E3BF44-0x00E3BF6C)
 *   - no store spills into the neighbouring cell of any 32-bit link
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
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

uint16_t PROC1_$CURRENT = 7;
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];

static int lock_starts;
static int lock_stops;
static int ec_waits;
static int grow_calls;
static int16_t grow_last_count;
static int16_t grow_adds_avail;
static int16_t wait_adds_avail;

static ec_$wait_ecs_t  last_wait_ecs;
static ec_$wait_vals_t last_wait_vals;

void ML_$EXCLUSION_START(ml_$exclusion_t *excl) { (void)excl; lock_starts++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)  { (void)excl; lock_stops++; }

/* Both 3-element arrays arrive BY VALUE (0x00E20610); see ec/ec.h. */
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    last_wait_ecs = ecs;
    last_wait_vals = vals;
    ec_waits++;
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) =
        (int16_t)(*(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) + wait_adds_avail);
    return 0;
}

void disk_$grow_qblk_pool(int16_t count)
{
    grow_calls++;
    grow_last_count = count;
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) =
        (int16_t)(*(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) + grow_adds_avail);
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../get_qblks_internal.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

/*
 * The free-list links are 32-bit target VAs, so the blocks have to live in
 * an arena that ARCH_HOST_VA_BASE points at (see arch/host/arch.h).  Block
 * VAs start at 0x40 so that a zero cell stays distinguishable from block 0.
 */
#define ARENA_BLOCKS    5
#define BLOCK_SIZE      0x40
#define BLOCK_BASE      0x40
#define RESERVE_VA      (BLOCK_BASE + ARENA_BLOCKS * BLOCK_SIZE)

static uint8_t arena[RESERVE_VA + BLOCK_SIZE];

#define BLOCK_VA(i)     ((uint32_t)(BLOCK_BASE + (i) * BLOCK_SIZE))
#define BLOCK(i)        (arena + BLOCK_VA(i))
#define RESERVE_BLOCK   (arena + RESERVE_VA)

#define D16(off)  (*(int16_t *)(DISK_$DATA + (off)))
#define D8(off)   (*(int8_t *)(DISK_$DATA + (off)))
#define DVA(off)  (*(uint32_t *)(DISK_$DATA + (off)))
#define REQ(i)    (((int16_t *)(DISK_$DATA + DMOD_REQ_QUEUE))[i])
#define BVA(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))

static void reset_module(void)
{
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(arena, 0xCC, sizeof(arena));
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));

    PROC1_$CURRENT = 7;

    lock_starts = 0;
    lock_stops = 0;
    ec_waits = 0;
    grow_calls = 0;
    grow_last_count = 0;
    grow_adds_avail = 0;
    wait_adds_avail = 0;

    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    D16(DMOD_REQ_WRITE_IDX) = 1;
    D16(DMOD_REQ_READ_IDX) = 1;
}

/* Link 'count' arena blocks into the module free list. */
static void setup_free_list(int count)
{
    for (int i = 0; i < count; i++) {
        BVA(BLOCK(i), DISK_QBLK_FREE_NEXT) =
            (i < count - 1) ? BLOCK_VA(i + 1) : 0u;
    }
    DVA(DMOD_FREE_HEAD) = count > 0 ? BLOCK_VA(0) : 0u;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(allocate_single_block)
{
    reset_module();
    setup_free_list(3);
    D16(DMOD_AVAIL_COUNT) = 3;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, 0, &first, &last);

    ASSERT_EQ((uintptr_t)BLOCK(0), (uintptr_t)first);
    ASSERT_EQ((uintptr_t)BLOCK(0), (uintptr_t)last);

    ASSERT_EQ(0u, *(uint32_t *)(BLOCK(0) + DISK_QBLK_STATUS));
    ASSERT_EQ(0u, *(uint16_t *)(BLOCK(0) + DISK_QBLK_FLAGS));
    ASSERT_EQ(7, *(BLOCK(0) + DISK_QBLK_OWNER));
    ASSERT_EQ(0, *(BLOCK(0) + DISK_QBLK_RESERVED));

    /* The single block is both first and last, so it is terminated. */
    ASSERT_EQ(0u, BVA(BLOCK(0), DISK_QBLK_FORWARD));
    ASSERT_EQ(0u, BVA(BLOCK(0), DISK_QBLK_FREE_NEXT));

    ASSERT_EQ(2, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(BLOCK_VA(1), DVA(DMOD_FREE_HEAD));

    ASSERT_EQ(1, lock_starts);
    ASSERT_EQ(1, lock_stops);
    ASSERT_EQ(0, ec_waits);
    ASSERT_EQ(0, grow_calls);
}

TEST(allocate_three_blocks_builds_the_forward_chain)
{
    reset_module();
    setup_free_list(5);
    D16(DMOD_AVAIL_COUNT) = 5;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(3, 0, &first, &last);

    ASSERT_EQ((uintptr_t)BLOCK(0), (uintptr_t)first);
    ASSERT_EQ((uintptr_t)BLOCK(2), (uintptr_t)last);

    ASSERT_EQ(BLOCK_VA(1), BVA(BLOCK(0), DISK_QBLK_FORWARD));
    ASSERT_EQ(BLOCK_VA(2), BVA(BLOCK(1), DISK_QBLK_FORWARD));

    /* Last block terminated on both links. */
    ASSERT_EQ(0u, BVA(BLOCK(2), DISK_QBLK_FORWARD));
    ASSERT_EQ(0u, BVA(BLOCK(2), DISK_QBLK_FREE_NEXT));

    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(0u, *(uint32_t *)(BLOCK(i) + DISK_QBLK_STATUS));
        ASSERT_EQ(0u, *(uint16_t *)(BLOCK(i) + DISK_QBLK_FLAGS));
        ASSERT_EQ(7, *(BLOCK(i) + DISK_QBLK_OWNER));
        ASSERT_EQ(0, *(BLOCK(i) + DISK_QBLK_RESERVED));
    }

    ASSERT_EQ(2, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(BLOCK_VA(3), DVA(DMOD_FREE_HEAD));
}

/*
 * The links are four bytes wide: writing DISK_QBLK_FORWARD (0x00) must not
 * disturb daddr at 0x04, and writing DISK_QBLK_FREE_NEXT (0x08) must not
 * disturb status at 0x0C.  A `void **` store would clobber both.
 */
TEST(link_stores_do_not_spill_into_the_neighbouring_cell)
{
    reset_module();
    setup_free_list(2);
    D16(DMOD_AVAIL_COUNT) = 2;
    *(uint32_t *)(BLOCK(0) + 0x04) = 0xA1A2A3A4u;
    *(uint32_t *)(BLOCK(1) + 0x04) = 0xB1B2B3B4u;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    /* daddr survives the forward-link store on both blocks. */
    ASSERT_EQ(0xA1A2A3A4u, *(uint32_t *)(BLOCK(0) + 0x04));
    ASSERT_EQ(0xB1B2B3B4u, *(uint32_t *)(BLOCK(1) + 0x04));

    /* status is cleared by the initialisation, not spilled into. */
    ASSERT_EQ(0u, *(uint32_t *)(BLOCK(1) + DISK_QBLK_STATUS));

    /* DMOD_RESERVE_BLOCK sits four bytes below DMOD_FREE_HEAD. */
    ASSERT_EQ(0u, DVA(DMOD_RESERVE_BLOCK));
}

TEST(owner_is_the_low_byte_of_the_current_process)
{
    reset_module();
    setup_free_list(2);
    D16(DMOD_AVAIL_COUNT) = 2;
    PROC1_$CURRENT = 0x142;     /* low byte 0x42 */

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, 0, &first, &last);

    ASSERT_EQ(0x42, *(BLOCK(0) + DISK_QBLK_OWNER));
}

TEST(write_mode_uses_the_reserve_block)
{
    reset_module();
    setup_free_list(0);
    BVA(RESERVE_BLOCK, DISK_QBLK_FREE_NEXT) = 0;

    DVA(DMOD_RESERVE_BLOCK) = RESERVE_VA;
    D8(DMOD_RESERVE_AVAIL) = -1;            /* st */
    D16(DMOD_AVAIL_COUNT) = 0;
    D8(DMOD_ALLOC_DISABLED) = -1;           /* growth disabled */

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, -1, &first, &last);

    ASSERT_EQ((uintptr_t)RESERVE_BLOCK, (uintptr_t)first);
    ASSERT_EQ((uintptr_t)RESERVE_BLOCK, (uintptr_t)last);

    /* 0x00E3BF02 copies the reserve VA into the head; 0x00E3BF08 clears the
     * flag.  The reserve cell itself is left alone. */
    ASSERT_EQ(RESERVE_VA, DVA(DMOD_RESERVE_BLOCK));
    ASSERT_EQ(0, D8(DMOD_RESERVE_AVAIL));

    ASSERT_EQ(0u, *(uint32_t *)(RESERVE_BLOCK + DISK_QBLK_STATUS));
    ASSERT_EQ(7, *(RESERVE_BLOCK + DISK_QBLK_OWNER));

    /* avail was never decremented: this path skips 0x00E3BEC8. */
    ASSERT_EQ(0, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(0, ec_waits);
    ASSERT_EQ(1, lock_stops);
}

TEST(write_mode_ignores_the_pending_count)
{
    reset_module();
    setup_free_list(3);
    D16(DMOD_AVAIL_COUNT) = 3;
    D16(DMOD_PENDING_COUNT) = 2;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, -1, &first, &last);

    ASSERT_EQ((uintptr_t)BLOCK(0), (uintptr_t)first);
    ASSERT_EQ((uintptr_t)BLOCK(1), (uintptr_t)last);
    ASSERT_EQ(1, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(0, ec_waits);
}

TEST(read_mode_enqueues_and_waits_when_requests_are_pending)
{
    reset_module();
    setup_free_list(5);
    D16(DMOD_AVAIL_COUNT) = 5;
    D16(DMOD_PENDING_COUNT) = 1;
    D8(DMOD_ALLOC_DISABLED) = -1;
    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    ASSERT_EQ(1, ec_waits);
    ASSERT_EQ(2, REQ(1));                       /* one-based queue */
    ASSERT_EQ(2, D16(DMOD_REQ_WRITE_IDX));
    ASSERT_EQ(2, D16(DMOD_PENDING_COUNT));

    /* wait_val = ec value + the new pending count (0x00E3BF38-0x00E3BF3E) */
    ASSERT_EQ(12, last_wait_vals.val[0]);
    ASSERT_EQ(0, last_wait_vals.val[1]);
    ASSERT_EQ(0, last_wait_vals.val[2]);
    ASSERT_EQ((uintptr_t)DISK_$DATA, (uintptr_t)last_wait_ecs.ec[0]);
    ASSERT_EQ(0, (uintptr_t)last_wait_ecs.ec[1]);
    ASSERT_EQ(0, (uintptr_t)last_wait_ecs.ec[2]);

    /* Lock dropped across the wait and retaken (0x00E3BF44 / 0x00E3BF6C). */
    ASSERT_EQ(2, lock_starts);
    ASSERT_EQ(2, lock_stops);
}

TEST(write_mode_without_a_reserve_waits_for_the_eventcount_plus_one)
{
    reset_module();
    setup_free_list(1);
    D16(DMOD_AVAIL_COUNT) = 0;
    D8(DMOD_RESERVE_AVAIL) = 0;             /* no reserve block available */
    D8(DMOD_ALLOC_DISABLED) = -1;           /* growth disabled */
    ((ec_$eventcount_t *)DISK_$DATA)->value = 20;
    wait_adds_avail = 1;                    /* the wait is satisfied */

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, -1, &first, &last);

    /* 0x00E3BF0E-0x00E3BF10: move.l (A5),D1 / addq.l #1,D1 */
    ASSERT_EQ(1, ec_waits);
    ASSERT_EQ(21, last_wait_vals.val[0]);
    ASSERT_EQ(0, grow_calls);

    /* Write mode loops back to 0x00E3BEB8 and allocates on the retry. */
    ASSERT_EQ((uintptr_t)BLOCK(0), (uintptr_t)first);
    ASSERT_EQ((uintptr_t)BLOCK(0), (uintptr_t)last);
    ASSERT_EQ(0, D16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(2, lock_starts);
    ASSERT_EQ(2, lock_stops);
    /* Nothing was enqueued: the request queue belongs to read mode. */
    ASSERT_EQ(0, D16(DMOD_PENDING_COUNT));
    ASSERT_EQ(1, D16(DMOD_REQ_WRITE_IDX));
}

TEST(request_queue_write_index_wraps_from_0x40_to_1)
{
    reset_module();
    setup_free_list(5);
    D16(DMOD_AVAIL_COUNT) = 0;
    D8(DMOD_ALLOC_DISABLED) = -1;
    D16(DMOD_PENDING_COUNT) = 1;
    D16(DMOD_REQ_WRITE_IDX) = DMOD_REQ_QUEUE_SIZE;   /* 0x40 */
    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(3, 0, &first, &last);

    ASSERT_EQ(1, D16(DMOD_REQ_WRITE_IDX));
    ASSERT_EQ(3, REQ(DMOD_REQ_QUEUE_SIZE));
    ASSERT_EQ(2, D16(DMOD_PENDING_COUNT));
    ASSERT_EQ(12, last_wait_vals.val[0]);
}

TEST(pool_is_grown_when_growth_is_enabled)
{
    reset_module();
    setup_free_list(5);
    D16(DMOD_AVAIL_COUNT) = 0;
    D8(DMOD_ALLOC_DISABLED) = 0;
    D16(DMOD_PENDING_COUNT) = 0;
    grow_adds_avail = 3;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    ASSERT_EQ(1, grow_calls);
    ASSERT_EQ(2, grow_last_count);
    ASSERT_EQ(0, ec_waits);
    ASSERT_EQ(1, D16(DMOD_AVAIL_COUNT));    /* 3 grown - 2 taken */
}

TEST(pool_is_not_grown_for_process_type_5)
{
    reset_module();
    setup_free_list(5);
    D16(DMOD_AVAIL_COUNT) = 0;
    D8(DMOD_ALLOC_DISABLED) = 0;
    D16(DMOD_PENDING_COUNT) = 0;
    PROC1_$TYPE[PROC1_$CURRENT] = 5;        /* 0x00E3BEE4 */
    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    ASSERT_EQ(0, grow_calls);
    ASSERT_EQ(1, ec_waits);
}

TEST(pool_is_not_grown_when_growth_is_disabled)
{
    reset_module();
    setup_free_list(5);
    D16(DMOD_AVAIL_COUNT) = 0;
    D8(DMOD_ALLOC_DISABLED) = -1;
    D16(DMOD_PENDING_COUNT) = 0;
    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    ASSERT_EQ(0, grow_calls);
    ASSERT_EQ(1, ec_waits);
    /* wait_val = ec value + pending (now 1) */
    ASSERT_EQ(11, last_wait_vals.val[0]);
}

int main(void)
{
    printf("test_get_qblks_internal:\n");

    RUN_TEST(allocate_single_block);
    RUN_TEST(allocate_three_blocks_builds_the_forward_chain);
    RUN_TEST(link_stores_do_not_spill_into_the_neighbouring_cell);
    RUN_TEST(owner_is_the_low_byte_of_the_current_process);
    RUN_TEST(write_mode_uses_the_reserve_block);
    RUN_TEST(write_mode_ignores_the_pending_count);
    RUN_TEST(read_mode_enqueues_and_waits_when_requests_are_pending);
    RUN_TEST(write_mode_without_a_reserve_waits_for_the_eventcount_plus_one);
    RUN_TEST(request_queue_write_index_wraps_from_0x40_to_1);
    RUN_TEST(pool_is_grown_when_growth_is_enabled);
    RUN_TEST(pool_is_not_grown_for_process_type_5);
    RUN_TEST(pool_is_not_grown_when_growth_is_disabled);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
