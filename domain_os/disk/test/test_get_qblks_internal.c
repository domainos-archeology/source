/*
 * disk/test/test_get_qblks_internal.c - Unit tests for disk_$get_qblks_internal
 *
 * Tests the queue block allocation function by mocking all subsystem
 * calls (ML_$EXCLUSION, EC_$WAIT, disk_$grow_qblk_pool) and verifying:
 *   - Simple allocation from a free list with sufficient blocks
 *   - Correct block initialization (status, flags, owner, reserved cleared)
 *   - Correct chain linking through forward pointers
 *   - Correct first_out and last_out outputs
 *   - Last block has NULL terminators
 *   - Single block allocation
 *   - Write mode with reserve block available
 *   - Read mode enqueues request when blocks unavailable
 *   - Pool growth attempted when blocks unavailable and not disabled
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Condition false at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf("FAILED\n    Expected NULL, got %p at line %d\n", (void*)(ptr), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ================================================================
 * Minimal types and mock definitions
 * ================================================================ */

typedef uint32_t status_$t;
#define status_$ok 0

/* Avoid system uid_t conflict */
#define uid_t disk_uid_t
typedef struct { uint32_t high; uint32_t low; } disk_uid_t;

/* Minimal ec_$eventcount_t */
typedef struct {
    int32_t value;
    void *waiter_list_head;
    void *waiter_list_tail;
} ec_$eventcount_t;

/* Minimal ml_$exclusion_t */
typedef struct { uint32_t data[4]; } ml_$exclusion_t;

/* EC_$WAIT argument records: two 3-element arrays passed BY VALUE
 * (0x00E20610); mirrors ec/ec.h, which this test does not include. */
typedef struct ec_$wait_ecs_t {
    ec_$eventcount_t *ec[3];
} ec_$wait_ecs_t;

typedef struct ec_$wait_vals_t {
    int32_t val[3];
} ec_$wait_vals_t;

/* ================================================================
 * Mock state
 * ================================================================ */

/* DISK_$DATA - mock disk module data area */
uint8_t DISK_$DATA[0xB00];

/* PROC1 globals */
uint16_t PROC1_$CURRENT = 7;
#define PROC1_MAX_PROCESSES 65
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];

/* Mock function call tracking */
static int mock_exclusion_start_count;
static int mock_exclusion_stop_count;
static int mock_ec_wait_count;
static int mock_grow_pool_count;
static int16_t mock_grow_pool_last_count;

/* For grow_pool mock: optionally add blocks on call */
static int mock_grow_pool_add_avail;

void ML_$EXCLUSION_START(ml_$exclusion_t *lock) {
    (void)lock;
    mock_exclusion_start_count++;
}

void ML_$EXCLUSION_STOP(ml_$exclusion_t *lock) {
    (void)lock;
    mock_exclusion_stop_count++;
}

/* Both 3-element arrays arrive BY VALUE (0xE20610); see ec/ec.h. */
static ec_$wait_ecs_t mock_ec_wait_last_ecs;
static ec_$wait_vals_t mock_ec_wait_last_vals;

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals) {
    mock_ec_wait_last_ecs = ecs;
    mock_ec_wait_last_vals = vals;
    mock_ec_wait_count++;
    return 0;
}

void disk_$grow_qblk_pool(int16_t count) {
    mock_grow_pool_count++;
    mock_grow_pool_last_count = count;

    /* If configured, add blocks to the available count */
    if (mock_grow_pool_add_avail > 0) {
        *(int16_t *)(DISK_$DATA + 0xAF8) += mock_grow_pool_add_avail;
    }
}

/* ================================================================
 * Suppress real headers, include implementation directly
 * ================================================================ */

#define DISK_INTERNAL_H
#define DISK_H
#define BASE_H
#define ML_H
#define EC_H
#define PROC1_H
#define PROC1_CONFIG_H
#define PROC2_H
#define DBUF_H
#define TIME_H

/* We need the DMOD_ and DISK_QBLK_ offset macros from the header */
#define DMOD_EVENTCOUNT       0x000
#define DMOD_REQ_QUEUE        0x00E
#define DMOD_EXCLUSION        0x090
#define DMOD_RESERVE_BLOCK    0x0BC
#define DMOD_FREE_HEAD        0x0C0
#define DMOD_PAGES_ALLOC      0xAF0
#define DMOD_REQ_READ_IDX     0xAF2
#define DMOD_REQ_WRITE_IDX    0xAF4
#define DMOD_PENDING_COUNT    0xAF6
#define DMOD_AVAIL_COUNT      0xAF8
#define DMOD_ALLOC_DISABLED   0xAFA
#define DMOD_RESERVE_AVAIL    0xAFC
#define DMOD_REQ_QUEUE_SIZE   0x40

#define DISK_QBLK_FORWARD     0x00
#define DISK_QBLK_FREE_NEXT   0x08
#define DISK_QBLK_STATUS      0x0C
#define DISK_QBLK_FLAGS       0x1C
#define DISK_QBLK_OWNER       0x1E
#define DISK_QBLK_RESERVED    0x1F

/* Create a stub disk_internal.h that the .c file will find via -I.
 * The real header is suppressed by DISK_INTERNAL_H guard above. */

/* Pull in the actual implementation.
 * The #include "disk/disk_internal.h" in the .c file is guarded by
 * DISK_INTERNAL_H which we defined above, so it will be a no-op. */
#define disk_disk_internal_h_already_handled
#include "../get_qblks_internal.c"

/* ================================================================
 * Test helpers
 * ================================================================ */

/* Mock queue blocks - 0x40 bytes each, large enough for all fields */
#define MOCK_BLOCK_SIZE 0x40
#define MAX_MOCK_BLOCKS 5
static uint8_t mock_blocks[MAX_MOCK_BLOCKS][MOCK_BLOCK_SIZE];

static void reset_mocks(void)
{
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(mock_blocks, 0xCC, sizeof(mock_blocks));
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));

    PROC1_$CURRENT = 7;

    mock_exclusion_start_count = 0;
    mock_exclusion_stop_count = 0;
    mock_ec_wait_count = 0;
    mock_grow_pool_count = 0;
    mock_grow_pool_last_count = 0;
    mock_grow_pool_add_avail = 0;
}

/* Set up a free list of 'count' mock blocks linked through DISK_QBLK_FREE_NEXT */
static void setup_free_list(int count)
{
    for (int i = 0; i < count && i < MAX_MOCK_BLOCKS; i++) {
        memset(mock_blocks[i], 0xCC, MOCK_BLOCK_SIZE);
        if (i < count - 1) {
            *(void **)(mock_blocks[i] + DISK_QBLK_FREE_NEXT) = mock_blocks[i + 1];
        } else {
            *(void **)(mock_blocks[i] + DISK_QBLK_FREE_NEXT) = NULL;
        }
    }
    /* Set free list head in module data */
    *(void **)(DISK_$DATA + DMOD_FREE_HEAD) = mock_blocks[0];
}

/* ================================================================
 * Tests
 * ================================================================ */

TEST(allocate_single_block)
{
    reset_mocks();
    setup_free_list(3);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 3;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, 0, &first, &last);

    /* First and last should both point to mock_blocks[0] */
    ASSERT_EQ((uintptr_t)mock_blocks[0], (uintptr_t)first);
    ASSERT_EQ((uintptr_t)mock_blocks[0], (uintptr_t)last);

    /* Block should be initialized */
    ASSERT_EQ(0, *(uint32_t *)(mock_blocks[0] + DISK_QBLK_STATUS));
    ASSERT_EQ(0, *(uint16_t *)(mock_blocks[0] + DISK_QBLK_FLAGS));
    ASSERT_EQ(7, *(mock_blocks[0] + DISK_QBLK_OWNER));
    ASSERT_EQ(0, *(mock_blocks[0] + DISK_QBLK_RESERVED));

    /* Last block should have NULL terminators */
    ASSERT_EQ(0, *(uint32_t *)(mock_blocks[0] + DISK_QBLK_FORWARD));
    ASSERT_EQ(0, *(uint32_t *)(mock_blocks[0] + DISK_QBLK_FREE_NEXT));

    /* Available count decremented */
    ASSERT_EQ(2, *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT));

    /* Free head advanced to next block */
    ASSERT_EQ((uintptr_t)mock_blocks[1], (uintptr_t)*(void **)(DISK_$DATA + DMOD_FREE_HEAD));

    /* Exclusion lock was acquired and released */
    ASSERT_EQ(1, mock_exclusion_start_count);
    ASSERT_EQ(1, mock_exclusion_stop_count);

    /* No waiting or pool growth needed */
    ASSERT_EQ(0, mock_ec_wait_count);
    ASSERT_EQ(0, mock_grow_pool_count);
}

TEST(allocate_three_blocks)
{
    reset_mocks();
    setup_free_list(5);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 5;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(3, 0, &first, &last);

    /* First should point to mock_blocks[0] */
    ASSERT_EQ((uintptr_t)mock_blocks[0], (uintptr_t)first);
    /* Last should point to mock_blocks[2] (the 3rd block) */
    ASSERT_EQ((uintptr_t)mock_blocks[2], (uintptr_t)last);

    /* Check forward chain: block[0]->forward = block[1], block[1]->forward = block[2] */
    ASSERT_EQ((uintptr_t)mock_blocks[1],
              *(uintptr_t *)(mock_blocks[0] + DISK_QBLK_FORWARD));
    ASSERT_EQ((uintptr_t)mock_blocks[2],
              *(uintptr_t *)(mock_blocks[1] + DISK_QBLK_FORWARD));

    /* Last block terminated */
    ASSERT_EQ(0, *(uint32_t *)(mock_blocks[2] + DISK_QBLK_FORWARD));
    ASSERT_EQ(0, *(uint32_t *)(mock_blocks[2] + DISK_QBLK_FREE_NEXT));

    /* All blocks initialized with correct owner */
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(0, *(uint32_t *)(mock_blocks[i] + DISK_QBLK_STATUS));
        ASSERT_EQ(0, *(uint16_t *)(mock_blocks[i] + DISK_QBLK_FLAGS));
        ASSERT_EQ(7, *(mock_blocks[i] + DISK_QBLK_OWNER));
        ASSERT_EQ(0, *(mock_blocks[i] + DISK_QBLK_RESERVED));
    }

    /* Available count decremented by 3 */
    ASSERT_EQ(2, *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT));

    /* Free head advanced past the 3 allocated blocks */
    ASSERT_EQ((uintptr_t)mock_blocks[3], (uintptr_t)*(void **)(DISK_$DATA + DMOD_FREE_HEAD));
}

TEST(blocks_initialized_with_different_owner)
{
    reset_mocks();
    setup_free_list(2);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 2;

    /* Set a different process ID */
    PROC1_$CURRENT = 42;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, 0, &first, &last);

    ASSERT_EQ(42, *(mock_blocks[0] + DISK_QBLK_OWNER));
}

TEST(write_mode_uses_reserve_block)
{
    reset_mocks();
    setup_free_list(0);

    /* Set up a reserve block */
    static uint8_t reserve_block[MOCK_BLOCK_SIZE];
    memset(reserve_block, 0xCC, MOCK_BLOCK_SIZE);
    *(void **)(reserve_block + DISK_QBLK_FREE_NEXT) = NULL;

    *(void **)(DISK_$DATA + DMOD_RESERVE_BLOCK) = reserve_block;
    *(uint8_t *)(DISK_$DATA + DMOD_RESERVE_AVAIL) = 0xFF;  /* st instruction */
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 0;  /* No blocks in main pool */
    *(uint8_t *)(DISK_$DATA + DMOD_ALLOC_DISABLED) = 0xFF;  /* Prevent grow loop */

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, -1, &first, &last);

    /* Should have used the reserve block */
    ASSERT_EQ((uintptr_t)reserve_block, (uintptr_t)first);
    ASSERT_EQ((uintptr_t)reserve_block, (uintptr_t)last);

    /* Reserve flag should be cleared */
    ASSERT_EQ(0, *(uint8_t *)(DISK_$DATA + DMOD_RESERVE_AVAIL));

    /* Block should be initialized */
    ASSERT_EQ(0, *(uint32_t *)(reserve_block + DISK_QBLK_STATUS));
    ASSERT_EQ(7, *(reserve_block + DISK_QBLK_OWNER));

    /* No waiting needed */
    ASSERT_EQ(0, mock_ec_wait_count);
}

TEST(write_mode_allocates_when_available)
{
    reset_mocks();
    setup_free_list(3);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 3;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, -1, &first, &last);

    /* Should allocate from main pool (write mode, but enough blocks) */
    ASSERT_EQ((uintptr_t)mock_blocks[0], (uintptr_t)first);
    ASSERT_EQ((uintptr_t)mock_blocks[1], (uintptr_t)last);
    ASSERT_EQ(1, *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT));
    ASSERT_EQ(0, mock_ec_wait_count);
}

TEST(write_mode_ignores_pending_count)
{
    reset_mocks();
    setup_free_list(3);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 3;
    *(int16_t *)(DISK_$DATA + DMOD_PENDING_COUNT) = 2;  /* Pending requests */

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, -1, &first, &last);

    /* Write mode should succeed despite pending requests */
    ASSERT_EQ((uintptr_t)mock_blocks[0], (uintptr_t)first);
    ASSERT_EQ(0, mock_ec_wait_count);
}

TEST(read_mode_blocked_by_pending)
{
    reset_mocks();
    setup_free_list(5);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 5;
    *(int16_t *)(DISK_$DATA + DMOD_PENDING_COUNT) = 1;  /* Has pending */
    *(uint8_t *)(DISK_$DATA + DMOD_ALLOC_DISABLED) = 0xFF;  /* Disabled - can't grow */
    *(int16_t *)(DISK_$DATA + DMOD_REQ_WRITE_IDX) = 1;

    /* Set eventcount value */
    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    /* Should have enqueued request, waited, then allocated */
    ASSERT_EQ(1, mock_ec_wait_count);

    /* Request should be enqueued: req_queue[1] = 2, write_idx advanced */
    ASSERT_EQ(2, *(int16_t *)(DISK_$DATA + DMOD_REQ_QUEUE + 1 * 2));
    ASSERT_EQ(2, *(int16_t *)(DISK_$DATA + DMOD_REQ_WRITE_IDX));

    /* Pending count incremented */
    ASSERT_EQ(2, *(int16_t *)(DISK_$DATA + DMOD_PENDING_COUNT));
}

TEST(grow_pool_called_when_not_disabled)
{
    reset_mocks();
    setup_free_list(5);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 0;  /* No blocks */
    *(uint8_t *)(DISK_$DATA + DMOD_ALLOC_DISABLED) = 0;  /* Not disabled */
    *(int16_t *)(DISK_$DATA + DMOD_PENDING_COUNT) = 0;  /* No pending */

    /* grow_pool mock will add 3 blocks */
    mock_grow_pool_add_avail = 3;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    /* Should have called grow_pool */
    ASSERT_TRUE(mock_grow_pool_count > 0);
    ASSERT_EQ(2, mock_grow_pool_last_count);

    /* No waiting needed since grow added enough blocks */
    ASSERT_EQ(0, mock_ec_wait_count);
}

TEST(grow_pool_skipped_for_process_type_5)
{
    reset_mocks();
    setup_free_list(5);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 0;
    *(uint8_t *)(DISK_$DATA + DMOD_ALLOC_DISABLED) = 0;
    *(int16_t *)(DISK_$DATA + DMOD_PENDING_COUNT) = 0;
    *(int16_t *)(DISK_$DATA + DMOD_REQ_WRITE_IDX) = 1;

    /* Set process type to 5 */
    PROC1_$TYPE[PROC1_$CURRENT] = 5;

    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    /* grow_pool should NOT have been called */
    ASSERT_EQ(0, mock_grow_pool_count);

    /* Should have fallen through to the wait path instead */
    ASSERT_EQ(1, mock_ec_wait_count);
}

TEST(grow_pool_skipped_when_disabled)
{
    reset_mocks();
    setup_free_list(5);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 0;
    *(uint8_t *)(DISK_$DATA + DMOD_ALLOC_DISABLED) = 0xFF;  /* Disabled */
    *(int16_t *)(DISK_$DATA + DMOD_PENDING_COUNT) = 0;
    *(int16_t *)(DISK_$DATA + DMOD_REQ_WRITE_IDX) = 1;

    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    /* grow_pool should NOT have been called */
    ASSERT_EQ(0, mock_grow_pool_count);

    /* Should have gone to wait path */
    ASSERT_EQ(1, mock_ec_wait_count);
}

TEST(request_queue_wraps_at_64)
{
    reset_mocks();
    setup_free_list(5);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 0;
    *(uint8_t *)(DISK_$DATA + DMOD_ALLOC_DISABLED) = 0xFF;  /* Can't grow */
    *(int16_t *)(DISK_$DATA + DMOD_PENDING_COUNT) = 1;
    *(int16_t *)(DISK_$DATA + DMOD_REQ_WRITE_IDX) = 0x40;  /* At max */

    ((ec_$eventcount_t *)DISK_$DATA)->value = 10;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(3, 0, &first, &last);

    /* Write index should have wrapped to 1 */
    ASSERT_EQ(1, *(int16_t *)(DISK_$DATA + DMOD_REQ_WRITE_IDX));

    /* Request enqueued at position 0x40 */
    ASSERT_EQ(3, *(int16_t *)(DISK_$DATA + DMOD_REQ_QUEUE + 0x40 * 2));
}

TEST(exclusion_lock_balanced)
{
    reset_mocks();
    setup_free_list(3);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 3;

    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(1, 0, &first, &last);

    /* Lock should be acquired once and released once */
    ASSERT_EQ(1, mock_exclusion_start_count);
    ASSERT_EQ(1, mock_exclusion_stop_count);
}

TEST(poison_fill_cleared_by_init)
{
    reset_mocks();
    setup_free_list(2);
    *(int16_t *)(DISK_$DATA + DMOD_AVAIL_COUNT) = 2;

    /* Blocks are poison-filled with 0xCC by setup_free_list.
     * Verify that the initialization clears the expected fields. */
    void *first = NULL, *last = NULL;
    disk_$get_qblks_internal(2, 0, &first, &last);

    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(0, *(uint32_t *)(mock_blocks[i] + DISK_QBLK_STATUS));
        ASSERT_EQ(0, *(uint16_t *)(mock_blocks[i] + DISK_QBLK_FLAGS));
        ASSERT_EQ(7, *(mock_blocks[i] + DISK_QBLK_OWNER));
        ASSERT_EQ(0, *(mock_blocks[i] + DISK_QBLK_RESERVED));
    }
}

/* ================================================================
 * Test runner
 * ================================================================ */

int main(void)
{
    printf("=== disk_$get_qblks_internal tests ===\n");

    RUN_TEST(allocate_single_block);
    RUN_TEST(allocate_three_blocks);
    RUN_TEST(blocks_initialized_with_different_owner);
    RUN_TEST(write_mode_uses_reserve_block);
    RUN_TEST(write_mode_allocates_when_available);
    RUN_TEST(write_mode_ignores_pending_count);
    RUN_TEST(read_mode_blocked_by_pending);
    RUN_TEST(grow_pool_called_when_not_disabled);
    RUN_TEST(grow_pool_skipped_for_process_type_5);
    RUN_TEST(grow_pool_skipped_when_disabled);
    RUN_TEST(request_queue_wraps_at_64);
    RUN_TEST(exclusion_lock_balanced);
    RUN_TEST(poison_fill_cleared_by_init);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
