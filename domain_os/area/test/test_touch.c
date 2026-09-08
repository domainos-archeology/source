/*
 * area/test/test_touch.c - Unit tests for AREA_$TOUCH (0x00E094FE) and
 *                          AREA_$ASSOC (0x00E096A2)
 *
 * The test #includes area/touch.c directly and drives both real routines
 * through mocked callees.
 *
 * The subject of bead source-xqqe is the tail at 0x00E0968C: it is not an
 * epilogue but a three-instruction stub that takes ML lock 0x14 and only
 * then falls into the register restore.  All four AREA_$TOUCH failure exits
 * branch to it -
 *
 *   0x00E0952C  bad or out-of-range area id
 *   0x00E09590  not active / generation mismatch with no remote UID
 *   0x00E095C0  area_$find_entry_by_uid failed
 *   0x00E09688  area_$resize failed (after dropping the ASTE reference)
 *
 * - so AREA_$TOUCH returns holding lock 0x14 on EVERY path, success
 * included (the success path takes it at 0x00E09648 and never releases it).
 * The tree returned without the lock on the first three.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
#define uid_t area_uid_t

#include "area/area_internal.h"

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-46s", #name);                                         \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

/* ==========================================================================
 * Mock area table
 * ========================================================================== */

static area_$entry_t mock_area_table[AREA_MAX_ENTRIES];

#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)

/* See area/test/test_copy.c: the record is wider than 0x30 on a 64-bit host. */
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

/* ==========================================================================
 * Module globals
 * ========================================================================== */

area_$globals_t AREA_$GLOBALS;
status_$t       Area_Internal_Error = 0x0032000A;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

#define MAX_LOCKS 16
static int      lock_calls;
static int      unlock_calls;
static int16_t  lock_ids[MAX_LOCKS];
static int16_t  unlock_ids[MAX_LOCKS];
/* +1 for each ML_$LOCK, -1 for each ML_$UNLOCK */
static int      lock_depth;

void ML_$LOCK(int16_t resource_id)
{
    if (lock_calls < MAX_LOCKS) {
        lock_ids[lock_calls] = resource_id;
    }
    lock_calls++;
    lock_depth++;
}

void ML_$UNLOCK(int16_t resource_id)
{
    if (unlock_calls < MAX_LOCKS) {
        unlock_ids[unlock_calls] = resource_id;
    }
    unlock_calls++;
    lock_depth--;
}

static int wait_in_trans_calls;

void area_$wait_in_trans(void)
{
    wait_in_trans_calls++;
    mock_area_table[0].flags &= (uint16_t)~AREA_FLAG_IN_TRANS;
}

/* area_$find_entry_by_uid */
static int        find_calls;
static int16_t    find_area_id;
static uint16_t  *find_bste_ptr;
static int16_t    find_page;
static status_$t  find_status;
static uint16_t   find_bste_out;      /* written back through the VAR word */
static int        find_rewrites_bste;
static aste_t     mock_aste;

aste_t *area_$find_entry_by_uid(int16_t area_id, uint16_t *bste_ptr,
                                int16_t page, status_$t *status_p)
{
    find_calls++;
    find_area_id = area_id;
    find_bste_ptr = bste_ptr;
    find_page = page;
    if (find_rewrites_bste) {
        *bste_ptr = find_bste_out;
    }
    *status_p = find_status;
    return &mock_aste;
}

/* area_$resize */
static int       resize_calls;
static int16_t   resize_area_id;
static uint32_t  resize_virt;
static uint32_t  resize_commit;
static int16_t   resize_is_grow;
static status_$t resize_status;

void area_$resize(int16_t area_id, area_$entry_t *entry,
                  uint32_t virt_size, uint32_t commit_size,
                  int16_t is_grow, status_$t *status_p)
{
    (void)entry;
    resize_calls++;
    resize_area_id = area_id;
    resize_virt = virt_size;
    resize_commit = commit_size;
    resize_is_grow = is_grow;
    *status_p = resize_status;
}

/* AST_$TOUCH_AREA */
static int        touch_area_calls;
static uint16_t   touch_area_area_id;
static uint16_t   touch_area_seg_index;
static int16_t    touch_area_page;
static uint32_t   touch_area_area_page;
static uint32_t  *touch_area_ppn;
static status_$t *touch_area_status_ptr;

void AST_$TOUCH_AREA(uint16_t area_id, uint16_t seg_index, int16_t page,
                     uint32_t area_page, uint32_t *ppn_array,
                     status_$t *status)
{
    touch_area_calls++;
    touch_area_area_id = area_id;
    touch_area_seg_index = seg_index;
    touch_area_page = page;
    touch_area_area_page = area_page;
    touch_area_ppn = ppn_array;
    touch_area_status_ptr = status;
}

/* AST_$ASSOC_AREA */
static int        assoc_area_calls;
static uint16_t   assoc_area_seg_index;
static int16_t    assoc_area_page;
static uint32_t   assoc_area_ppn;
static status_$t *assoc_area_status_ptr;

void AST_$ASSOC_AREA(uint16_t seg_index, int16_t page, uint32_t ppn,
                     status_$t *status)
{
    assoc_area_calls++;
    assoc_area_seg_index = seg_index;
    assoc_area_page = page;
    assoc_area_ppn = ppn;
    assoc_area_status_ptr = status;
}

/* ==========================================================================
 * The unit under test
 * ========================================================================== */

#include "../touch.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

#define AREA_ID  1
#define GEN      0x77

static area_$entry_t *entry_of(void) { return &mock_area_table[AREA_ID - 1]; }

static area_$handle_t handle;
static uint32_t       ppn_array[8];

static void reset_mocks(void)
{
    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(&mock_aste, 0, sizeof(mock_aste));
    memset(ppn_array, 0, sizeof(ppn_array));

    lock_calls = unlock_calls = lock_depth = 0;
    memset(lock_ids, 0, sizeof(lock_ids));
    memset(unlock_ids, 0, sizeof(unlock_ids));
    wait_in_trans_calls = 0;
    find_calls = 0;
    find_status = status_$ok;
    find_rewrites_bste = 0;
    find_bste_out = 0;
    resize_calls = 0;
    resize_status = status_$ok;
    touch_area_calls = 0;
    assoc_area_calls = 0;

    AREA_$N_AREAS = AREA_MAX_ENTRIES;

    handle = AREA_MAKE_HANDLE(GEN, AREA_ID);

    entry_of()->flags = AREA_FLAG_ACTIVE;
    entry_of()->generation = GEN;
    entry_of()->virt_size = 0x100000;
    entry_of()->commit_size = 0x8000;    /* 32 pages */

    mock_aste.seg_index = 0x0123;
    mock_aste.wire_count = 4;
}

/* ==========================================================================
 * AREA_$TOUCH tests
 * ========================================================================== */

/* 0x00E0951C -> 0x00E09524 -> 0x00E0968C */
TEST(bad_area_id_still_takes_lock_0x14)
{
    status_$t status = 0;
    area_$handle_t bad = AREA_MAKE_HANDLE(GEN, 0);

    AREA_$TOUCH(&bad, 0, 0, 0, ppn_array, &status);

    ASSERT_EQ(status_$area_not_active, status);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(ML_LOCK_PMAP, lock_ids[0]);
    ASSERT_EQ(0, unlock_calls);
    ASSERT_EQ(1, lock_depth);            /* held on return */
    ASSERT_EQ(0, find_calls);
}

/* 0x00E09566 -> 0x00E0957A -> 0x00E0968C */
TEST(not_active_unlocks_0x0e_then_takes_lock_0x14)
{
    status_$t status = 0;

    entry_of()->flags = 0;

    AREA_$TOUCH(&handle, 0, 0, 0, ppn_array, &status);

    ASSERT_EQ(status_$area_not_active, status);
    /* 0x0E in, 0x0E out, then 0x14 in and no matching release */
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(ML_LOCK_AREA, lock_ids[0]);
    ASSERT_EQ(ML_LOCK_PMAP, lock_ids[1]);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(ML_LOCK_AREA, unlock_ids[0]);
    ASSERT_EQ(1, lock_depth);
    ASSERT_EQ(0, find_calls);
}

/*
 * 0x00E0956C-0x00E09578: a generation mismatch is forgiven when the area
 * has a remote UID.
 */
TEST(generation_mismatch_with_remote_uid_proceeds)
{
    status_$t status = 0;
    area_$handle_t wrong = AREA_MAKE_HANDLE(GEN + 1, AREA_ID);

    entry_of()->remote_uid = 0xDEADBEEF;

    AREA_$TOUCH(&wrong, 0, 0, 0, ppn_array, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, find_calls);
    ASSERT_EQ(1, touch_area_calls);
}

/* 0x00E095BE -> 0x00E0968C */
TEST(find_entry_failure_still_takes_lock_0x14)
{
    status_$t status = 0;

    find_status = 0x00030001;

    AREA_$TOUCH(&handle, 0, 0, 0, ppn_array, &status);

    ASSERT_EQ(0x00030001, status);
    ASSERT_EQ(1, find_calls);
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(ML_LOCK_PMAP, lock_ids[1]);
    ASSERT_EQ(1, lock_depth);
    ASSERT_EQ(0, resize_calls);
    ASSERT_EQ(0, touch_area_calls);
    /* the reference is NOT dropped on this path */
    ASSERT_EQ(4, mock_aste.wire_count);
}

/* 0x00E09644 -> 0x00E09688 -> 0x00E0968C */
TEST(resize_failure_drops_the_reference_then_takes_lock_0x14)
{
    status_$t status = 0;

    /* commit_size 32 pages, so page 40 needs a grow */
    resize_status = 0x00320005;

    AREA_$TOUCH(&handle, 1, 8, 0, ppn_array, &status);

    ASSERT_EQ(0x00320005, status);
    ASSERT_EQ(1, resize_calls);
    ASSERT_EQ(3, mock_aste.wire_count);      /* 0x00E09688 subq.b #1 */
    ASSERT_EQ(0, touch_area_calls);
    ASSERT_EQ(ML_LOCK_PMAP, lock_ids[lock_calls - 1]);
    ASSERT_EQ(1, lock_depth);
}

/* 0x00E09648: the success path also returns holding 0x14 */
TEST(success_returns_holding_lock_0x14)
{
    status_$t status = 0;

    AREA_$TOUCH(&handle, 0, 3, 0, ppn_array, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, resize_calls);              /* page 3 is inside 32 pages */
    ASSERT_EQ(1, touch_area_calls);
    ASSERT_EQ(1, lock_depth);
    ASSERT_EQ(ML_LOCK_PMAP, lock_ids[lock_calls - 1]);
    /* 0x00E0967C */
    ASSERT_TRUE((entry_of()->flags & AREA_FLAG_TOUCHED) != 0);
    /* 0x00E09682 */
    ASSERT_EQ(3, mock_aste.wire_count);
}

/* 0x00E09656-0x00E09676 */
TEST(touch_area_argument_list)
{
    status_$t status = 0;

    AREA_$TOUCH(&handle, 0, 3, 0, ppn_array, &status);

    ASSERT_EQ(AREA_ID, touch_area_area_id);
    ASSERT_EQ(0x0123, touch_area_seg_index);
    ASSERT_EQ(3, touch_area_page);
    ASSERT_EQ(3, touch_area_area_page);
    ASSERT_EQ((uintptr_t)ppn_array, (uintptr_t)touch_area_ppn);
    ASSERT_EQ((uintptr_t)&status, (uintptr_t)touch_area_status_ptr);
}

/*
 * 0x00E095A6 `pea (0xc,A6)` and 0x00E095BA `move.w (0xc,A6),D4w`: the block
 * index is a VAR parameter and is re-read after the call, so a rewritten
 * value is what the grow arithmetic and the area page number use.
 */
TEST(bste_index_is_re_read_after_find_entry)
{
    status_$t status = 0;

    find_rewrites_bste = 1;
    find_bste_out = 2;                       /* 2*32 + 1 = page 65 */

    AREA_$TOUCH(&handle, 0, 1, 0, ppn_array, &status);

    ASSERT_EQ((uintptr_t)find_bste_ptr, (uintptr_t)find_bste_ptr);
    ASSERT_EQ(65, touch_area_area_page);
    /* 65 - 32 + 1 = 34 pages needed, so the grow uses 34 not the 4 floor */
    ASSERT_EQ(1, resize_calls);
    ASSERT_EQ(0x8000u + (34u << 10), resize_commit);
}

/* 0x00E0960C-0x00E09612: at least four pages */
TEST(grow_has_a_four_page_floor)
{
    status_$t status = 0;

    /* page 32 is one past the committed 32 -> needed == 1 */
    AREA_$TOUCH(&handle, 1, 0, 0, ppn_array, &status);

    ASSERT_EQ(1, resize_calls);
    ASSERT_EQ(AREA_ID, resize_area_id);
    ASSERT_EQ(0x100000u, resize_virt);
    ASSERT_EQ(0x8000u + (4u << 10), resize_commit);
    ASSERT_EQ(1, resize_is_grow);
}

/* 0x00E09624-0x00E0962C: clamped to virt_size */
TEST(grow_is_clamped_to_virt_size)
{
    status_$t status = 0;

    entry_of()->virt_size = 0x8400;          /* only one page of headroom */

    AREA_$TOUCH(&handle, 1, 0, 0, ppn_array, &status);

    ASSERT_EQ(1, resize_calls);
    ASSERT_EQ(0x8400u, resize_commit);
}

/* 0x00E095CE-0x00E095EC: the reversed-area page arithmetic */
TEST(reversed_area_grow_arithmetic)
{
    status_$t status = 0;

    entry_of()->flags |= AREA_FLAG_REVERSED;

    /* (1<<5) + 31 - 0 - 32 + 1 = 32 pages needed */
    AREA_$TOUCH(&handle, 1, 0, 0, ppn_array, &status);

    ASSERT_EQ(1, resize_calls);
    ASSERT_EQ(0x8000u + (32u << 10), resize_commit);
}

/* ==========================================================================
 * AREA_$ASSOC tests
 * ========================================================================== */

/* 0x00E096B0-0x00E096C6 */
TEST(assoc_bad_area_id_returns_without_any_lock)
{
    status_$t status = 0;

    AREA_$ASSOC(0, 0, 0, 0, &status);

    ASSERT_EQ(status_$area_not_active, status);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, find_calls);
}

/* 0x00E096DE */
TEST(assoc_find_failure_returns_without_any_lock)
{
    status_$t status = 0;

    find_status = 0x00030001;

    AREA_$ASSOC(AREA_ID, 0, 0, 0, &status);

    ASSERT_EQ(0x00030001, status);
    ASSERT_EQ(1, find_calls);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, assoc_area_calls);
}

/* 0x00E096C8-0x00E09718 */
TEST(assoc_success_brackets_with_lock_0x14)
{
    status_$t status = 0;

    AREA_$ASSOC(AREA_ID, 5, 6, 0x1234, &status);

    ASSERT_EQ(AREA_ID, find_area_id);
    ASSERT_EQ(6, find_page);
    ASSERT_EQ(1, assoc_area_calls);
    ASSERT_EQ(0x0123, assoc_area_seg_index);
    ASSERT_EQ(6, assoc_area_page);
    ASSERT_EQ(0x1234u, assoc_area_ppn);
    ASSERT_EQ((uintptr_t)&status, (uintptr_t)assoc_area_status_ptr);
    ASSERT_EQ(3, mock_aste.wire_count);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(ML_LOCK_PMAP, lock_ids[0]);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(ML_LOCK_PMAP, unlock_ids[0]);
    ASSERT_EQ(0, lock_depth);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("AREA_$TOUCH / AREA_$ASSOC tests:\n");

    RUN_TEST(bad_area_id_still_takes_lock_0x14);
    RUN_TEST(not_active_unlocks_0x0e_then_takes_lock_0x14);
    RUN_TEST(generation_mismatch_with_remote_uid_proceeds);
    RUN_TEST(find_entry_failure_still_takes_lock_0x14);
    RUN_TEST(resize_failure_drops_the_reference_then_takes_lock_0x14);
    RUN_TEST(success_returns_holding_lock_0x14);
    RUN_TEST(touch_area_argument_list);
    RUN_TEST(bste_index_is_re_read_after_find_entry);
    RUN_TEST(grow_has_a_four_page_floor);
    RUN_TEST(grow_is_clamped_to_virt_size);
    RUN_TEST(reversed_area_grow_arithmetic);
    RUN_TEST(assoc_bad_area_id_returns_without_any_lock);
    RUN_TEST(assoc_find_failure_returns_without_any_lock);
    RUN_TEST(assoc_success_brackets_with_lock_0x14);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
