/*
 * bat/test/test_n_free.c - BAT_$N_FREE (0x00E3B9E4).
 *
 * The property under test is which paths touch the caller's cells.  The
 * epilogue at 0x00E3BA5A-0x00E3BA66 stores D3, D2 and the status with no
 * test in front of it, so the not-mounted arm writes free_out and total_out
 * too; only the diskless arm at 0x00E3BA04 branches past all three stores.
 * The earlier C returned early from the not-mounted arm and left the cells
 * alone.
 *
 * The values that arm writes are register residue and cannot be asserted -
 * see the comment in bat/n_free.c - so the tests check which cells were
 * disturbed, plus the lock discipline that distinguishes the two arms
 * (the diskless arm takes no lock at all).
 */

#include "bat/bat_internal.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
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

#define ASSERT_STATUS(expected, actual) \
    ASSERT_EQ((uint32_t)(expected), (uint32_t)(actual))

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

int8_t NETWORK_$REALLY_DISKLESS;

static bat_$volume_t volumes_storage[BAT_MAX_VOLUMES];
static int8_t mounted_storage[BAT_MAX_VOLUMES];

#define bat_$volumes volumes_storage
#define bat_$mounted mounted_storage

static int lock_calls;
static int unlock_calls;
static int16_t last_lock_id;

void ML_$LOCK(int16_t lock_id)   { lock_calls++;   last_lock_id = lock_id; }
void ML_$UNLOCK(int16_t lock_id) { unlock_calls++; last_lock_id = lock_id; }

#include "../n_free.c"

/* ============================================================================
 * Tests
 * ============================================================================ */

static void reset(void)
{
    memset(volumes_storage, 0, sizeof(volumes_storage));
    memset(mounted_storage, 0, sizeof(mounted_storage));
    NETWORK_$REALLY_DISKLESS = 0;
    lock_calls = 0;
    unlock_calls = 0;
}

/* The mounted arm: free_blocks from +0x04, total_blocks from +0x00. */
TEST(mounted_volume_returns_both_counts)
{
    uint16_t vol = 3;
    uint32_t free_out = 0xAAAAAAAAu;
    uint32_t total_out = 0xBBBBBBBBu;
    status_$t st = 0x7F7F7F7F;

    reset();
    mounted_storage[3] = (int8_t)0xFF;          /* 0x00E3B792 st.b */
    volumes_storage[3].free_blocks = 0x00001234u;
    volumes_storage[3].total_blocks = 0x00005678u;

    BAT_$N_FREE(&vol, &free_out, &total_out, &st);

    ASSERT_EQ(0x00001234u, free_out);
    ASSERT_EQ(0x00005678u, total_out);
    ASSERT_STATUS(status_$ok, st);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(ML_LOCK_BAT, last_lock_id);
}

/*
 * The not-mounted arm still runs the epilogue: the status is written and
 * the lock is released.  0x00E3BA5A-0x00E3BA64 also stores the two
 * registers, which the code under test reproduces; their contents are
 * indeterminate on this path in the image too, so only the cells that
 * carry meaning are asserted.
 */
TEST(unmounted_volume_still_unlocks_and_sets_status)
{
    uint16_t vol = 2;
    uint32_t free_out = 0;
    uint32_t total_out = 0;
    status_$t st = 0;

    reset();
    mounted_storage[2] = 0;                     /* not mounted */

    BAT_$N_FREE(&vol, &free_out, &total_out, &st);

    ASSERT_STATUS(bat_$not_mounted, st);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);                 /* 0x00E3BA4E */
}

/* 0x00E3BA1A beq: index 0 is rejected before the mount byte is read. */
TEST(volume_zero_is_rejected)
{
    uint16_t vol = 0;
    uint32_t free_out = 0;
    uint32_t total_out = 0;
    status_$t st = 0;

    reset();

    BAT_$N_FREE(&vol, &free_out, &total_out, &st);

    ASSERT_STATUS(bat_$not_mounted, st);
    ASSERT_EQ(1, unlock_calls);
}

/* 0x00E3BA1E cmpi.w #0x6 / bhi: 6 is the last legal index. */
TEST(volume_seven_is_rejected)
{
    uint16_t vol = 7;
    uint32_t free_out = 0;
    uint32_t total_out = 0;
    status_$t st = 0;

    reset();

    BAT_$N_FREE(&vol, &free_out, &total_out, &st);

    ASSERT_STATUS(bat_$not_mounted, st);
    ASSERT_EQ(1, unlock_calls);
}

/*
 * The diskless arm (0x00E3B9FC tst.b / bmi -> 0x00E3BA04) is the only one
 * that reaches the rts without touching free_out, total_out or the lock.
 */
TEST(diskless_leaves_the_out_cells_and_the_lock_alone)
{
    uint16_t vol = 3;
    uint32_t free_out = 0xDEADBEEFu;
    uint32_t total_out = 0xFEEDFACEu;
    status_$t st = 0;

    reset();
    NETWORK_$REALLY_DISKLESS = (int8_t)0x80;
    mounted_storage[3] = (int8_t)0xFF;
    volumes_storage[3].free_blocks = 1;
    volumes_storage[3].total_blocks = 2;

    BAT_$N_FREE(&vol, &free_out, &total_out, &st);

    ASSERT_STATUS(bat_$not_mounted, st);
    ASSERT_EQ(0xDEADBEEFu, free_out);
    ASSERT_EQ(0xFEEDFACEu, total_out);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, unlock_calls);
}

int main(void)
{
    printf("BAT_$N_FREE tests\n");
    RUN_TEST(mounted_volume_returns_both_counts);
    RUN_TEST(unmounted_volume_still_unlocks_and_sets_status);
    RUN_TEST(volume_zero_is_rejected);
    RUN_TEST(volume_seven_is_rejected);
    RUN_TEST(diskless_leaves_the_out_cells_and_the_lock_alone);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
