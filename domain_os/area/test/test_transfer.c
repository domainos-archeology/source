/*
 * area/test/test_transfer.c - Unit tests for AREA_$TRANSFER (0x00E08098)
 *
 * The test #includes area/transfer.c directly and drives the real
 * AREA_$TRANSFER through mocked callees.  Its main subject is bead
 * source-zm73: the return value.
 *
 * D2 holds `new_asid` from the prologue (0x00E080AA) all the way to
 * 0x00E08274, where the fully successful path overwrites it with D6 - the
 * area's OLD first_seg_index, saved at 0x00E0818E.  0x00E0829C then returns
 * D2.  So:
 *
 *   success            -> the old first_seg_index
 *   every failure path -> new_asid, unchanged
 *
 * The tree had these two the wrong way round.  The tests also cover the
 * list re-threading at 0x00E0822C-0x00E0826C, the reversed-area segment
 * adjustment at 0x00E081A8, and the revert at 0x00E081E4-0x00E0820C.
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

uint16_t PROC1_$AS_ID;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

#define MAX_LOCKS 16
static int      lock_calls;
static int      unlock_calls;
static int16_t  lock_ids[MAX_LOCKS];
static int16_t  unlock_ids[MAX_LOCKS];

void ML_$LOCK(int16_t resource_id)
{
    if (lock_calls < MAX_LOCKS) {
        lock_ids[lock_calls] = resource_id;
    }
    lock_calls++;
}

void ML_$UNLOCK(int16_t resource_id)
{
    if (unlock_calls < MAX_LOCKS) {
        unlock_ids[unlock_calls] = resource_id;
    }
    unlock_calls++;
}

static int ec_advance_calls;
static ec_$eventcount_t *ec_advance_last;

void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    ec_advance_calls++;
    ec_advance_last = ec;
}

static int wait_in_trans_calls;

void area_$wait_in_trans(void)
{
    wait_in_trans_calls++;
    mock_area_table[0].flags &= (uint16_t)~AREA_FLAG_IN_TRANS;
}

#define MAX_RESIZE 4
static int       resize_calls;
static int16_t   resize_area_id[MAX_RESIZE];
static uint32_t  resize_virt[MAX_RESIZE];
static uint32_t  resize_commit[MAX_RESIZE];
static int16_t   resize_is_grow[MAX_RESIZE];
static status_$t resize_status[MAX_RESIZE];

void area_$resize(int16_t area_id, area_$entry_t *entry,
                  uint32_t virt_size, uint32_t commit_size,
                  int16_t is_grow, status_$t *status_p)
{
    int i = resize_calls;

    (void)entry;
    if (i < MAX_RESIZE) {
        resize_area_id[i] = area_id;
        resize_virt[i] = virt_size;
        resize_commit[i] = commit_size;
        resize_is_grow[i] = is_grow;
        *status_p = resize_status[i];
    }
    resize_calls++;
}

/* ==========================================================================
 * The unit under test
 * ========================================================================== */

#include "../transfer.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

#define AREA_ID     1
#define OLD_ASID    3
#define NEW_ASID    7
#define OLD_SEG     0x0041

static area_$entry_t *entry_of(void) { return &mock_area_table[AREA_ID - 1]; }

static area_$handle_t handle;

static void reset_mocks(void)
{
    int i;

    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));

    lock_calls = unlock_calls = 0;
    memset(lock_ids, 0, sizeof(lock_ids));
    memset(unlock_ids, 0, sizeof(unlock_ids));
    ec_advance_calls = 0;
    ec_advance_last = NULL;
    wait_in_trans_calls = 0;
    resize_calls = 0;
    for (i = 0; i < MAX_RESIZE; i++) {
        resize_status[i] = status_$ok;
    }

    PROC1_$AS_ID = OLD_ASID;
    AREA_$N_AREAS = AREA_MAX_ENTRIES;

    handle = AREA_MAKE_HANDLE(0x99, AREA_ID);

    entry_of()->flags = AREA_FLAG_ACTIVE;
    entry_of()->generation = 0x99;
    entry_of()->owner_asid = OLD_ASID;
    entry_of()->first_seg_index = OLD_SEG;
    entry_of()->first_bste = OLD_ASID;
    entry_of()->virt_size = 0x10000;
    entry_of()->commit_size = 0x2000;

    AREA_$ASID_LIST[OLD_ASID] = entry_of();
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E08274 then 0x00E0829C */
TEST(success_returns_the_old_first_seg_index)
{
    status_$t status = 0x1111;
    int16_t r;

    r = AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x10000, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(OLD_SEG, r);
    ASSERT_EQ(NEW_ASID, entry_of()->owner_asid);
    ASSERT_EQ(NEW_ASID, entry_of()->first_bste);
    ASSERT_EQ(0x0002, entry_of()->first_seg_index);
    ASSERT_EQ(1, ec_advance_calls);
    ASSERT_EQ((uintptr_t)&AREA_$IN_TRANS_EC, (uintptr_t)ec_advance_last);
}

/* 0x00E080C2: D2 is still new_asid here */
TEST(bad_area_id_returns_new_asid)
{
    status_$t status = 0;
    area_$handle_t bad = AREA_MAKE_HANDLE(0x99, 0);
    int16_t r;

    r = AREA_$TRANSFER(&bad, NEW_ASID, 0x0002, 0x10000, &status);

    ASSERT_EQ(status_$area_not_active, status);
    ASSERT_EQ(NEW_ASID, r);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, ec_advance_calls);
}

/* 0x00E08110 */
TEST(inactive_area_returns_new_asid)
{
    status_$t status = 0;
    int16_t r;

    entry_of()->flags = 0;

    r = AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x10000, &status);

    ASSERT_EQ(status_$area_not_active, status);
    ASSERT_EQ(NEW_ASID, r);
    /* 0x00E08288 unlocks 0x0E without ever reaching the tail's EC advance */
    ASSERT_EQ(0, ec_advance_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(ML_LOCK_AREA, unlock_ids[0]);
}

/* 0x00E08128 */
TEST(not_owner_returns_new_asid)
{
    status_$t status = 0;
    int16_t r;

    PROC1_$AS_ID = OLD_ASID + 1;

    r = AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x10000, &status);

    ASSERT_EQ(status_$area_not_owner, status);
    ASSERT_EQ(NEW_ASID, r);
    ASSERT_EQ(0, ec_advance_calls);
    ASSERT_EQ(OLD_ASID, entry_of()->owner_asid);
}

/* 0x00E0814C-0x00E08170: the shrink resize fails before anything moves */
TEST(shrink_resize_failure_returns_new_asid)
{
    status_$t status = 0;
    int16_t r;

    resize_status[0] = 0x00320005;

    r = AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x1000, &status);

    ASSERT_EQ(0x00320005, status);
    ASSERT_EQ(NEW_ASID, r);
    ASSERT_EQ(1, resize_calls);
    ASSERT_EQ(0x1000, resize_virt[0]);
    ASSERT_EQ(0x2000, resize_commit[0]);
    ASSERT_EQ(1, resize_is_grow[0]);
    /* The entry never left the old ASID's list. */
    ASSERT_EQ(OLD_ASID, entry_of()->owner_asid);
    ASSERT_EQ(OLD_SEG, entry_of()->first_seg_index);
    /* 0x00E0820E takes 0x0E again, then the tail clears IN_TRANS */
    ASSERT_EQ(0, (entry_of()->flags & AREA_FLAG_IN_TRANS));
    ASSERT_EQ(1, ec_advance_calls);
}

/* 0x00E081C2-0x00E0820C: the grow resize fails and the move is reverted */
TEST(grow_resize_failure_reverts_and_returns_new_asid)
{
    status_$t status = 0;
    int16_t r;

    resize_status[0] = 0x00320005;

    r = AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x20000, &status);

    ASSERT_EQ(0x00320005, status);
    ASSERT_EQ(NEW_ASID, r);
    ASSERT_EQ(1, resize_calls);
    /* 0x00E081F4/0x00E081FC put PROC1_$AS_ID and the old index back */
    ASSERT_EQ(PROC1_$AS_ID, entry_of()->first_bste);
    ASSERT_EQ(OLD_SEG, entry_of()->first_seg_index);
    ASSERT_EQ(OLD_ASID, entry_of()->owner_asid);
    ASSERT_EQ(1, ec_advance_calls);
}

/* 0x00E081A8-0x00E081AC */
TEST(reversed_area_adds_the_segment_adjustment)
{
    status_$t status = 0;

    entry_of()->flags |= AREA_FLAG_REVERSED;
    entry_of()->virt_size = 0x18000;    /* 3 * 32K -> adjustment 2 */

    AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x18000, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x0004, entry_of()->first_seg_index);
}

/* 0x00E0822C-0x00E0826C: unlink from the head of the old list */
TEST(relink_from_list_head)
{
    status_$t status = 0;
    area_$entry_t next_entry;

    memset(&next_entry, 0, sizeof(next_entry));
    entry_of()->next = &next_entry;
    entry_of()->prev = NULL;
    next_entry.prev = entry_of();
    AREA_$ASID_LIST[OLD_ASID] = entry_of();
    AREA_$ASID_LIST[NEW_ASID] = NULL;

    AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x10000, &status);

    ASSERT_EQ((uintptr_t)&next_entry, (uintptr_t)AREA_$ASID_LIST[OLD_ASID]);
    ASSERT_EQ(0, (uintptr_t)next_entry.prev);
    ASSERT_EQ((uintptr_t)entry_of(), (uintptr_t)AREA_$ASID_LIST[NEW_ASID]);
    ASSERT_EQ(0, (uintptr_t)entry_of()->next);
    ASSERT_EQ(0, (uintptr_t)entry_of()->prev);
}

/* 0x00E0823E-0x00E08244: unlink from the middle of the old list */
TEST(relink_from_list_middle)
{
    status_$t status = 0;
    area_$entry_t prev_entry;
    area_$entry_t next_entry;
    area_$entry_t head_of_new;

    memset(&prev_entry, 0, sizeof(prev_entry));
    memset(&next_entry, 0, sizeof(next_entry));
    memset(&head_of_new, 0, sizeof(head_of_new));

    prev_entry.next = entry_of();
    entry_of()->prev = &prev_entry;
    entry_of()->next = &next_entry;
    next_entry.prev = entry_of();
    AREA_$ASID_LIST[OLD_ASID] = &prev_entry;
    AREA_$ASID_LIST[NEW_ASID] = &head_of_new;

    AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x10000, &status);

    /* the old list head is untouched */
    ASSERT_EQ((uintptr_t)&prev_entry, (uintptr_t)AREA_$ASID_LIST[OLD_ASID]);
    ASSERT_EQ((uintptr_t)&next_entry, (uintptr_t)prev_entry.next);
    ASSERT_EQ((uintptr_t)&prev_entry, (uintptr_t)next_entry.prev);
    /* pushed onto the front of the new list */
    ASSERT_EQ((uintptr_t)entry_of(), (uintptr_t)AREA_$ASID_LIST[NEW_ASID]);
    ASSERT_EQ((uintptr_t)&head_of_new, (uintptr_t)entry_of()->next);
    ASSERT_EQ((uintptr_t)entry_of(), (uintptr_t)head_of_new.prev);
    ASSERT_EQ(0, (uintptr_t)entry_of()->prev);
}

/* 0x00E08174/0x00E081B0: the segment-index update runs under lock 0x14 */
TEST(lock_sequence_on_success)
{
    status_$t status = 0;

    AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x10000, &status);

    /* 0x0E (entry), 0x14 (re-base), 0x0E (relink) */
    ASSERT_EQ(3, lock_calls);
    ASSERT_EQ(ML_LOCK_AREA, lock_ids[0]);
    ASSERT_EQ(ML_LOCK_PMAP, lock_ids[1]);
    ASSERT_EQ(ML_LOCK_AREA, lock_ids[2]);
    ASSERT_EQ(3, unlock_calls);
    ASSERT_EQ(ML_LOCK_AREA, unlock_ids[0]);
    ASSERT_EQ(ML_LOCK_PMAP, unlock_ids[1]);
    ASSERT_EQ(ML_LOCK_AREA, unlock_ids[2]);
}

/* 0x00E080FE-0x00E08108 */
TEST(waits_for_in_transition)
{
    status_$t status = 0;

    entry_of()->flags |= AREA_FLAG_IN_TRANS;

    AREA_$TRANSFER(&handle, NEW_ASID, 0x0002, 0x10000, &status);

    ASSERT_EQ(1, wait_in_trans_calls);
    ASSERT_EQ(status_$ok, status);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("AREA_$TRANSFER tests:\n");

    RUN_TEST(success_returns_the_old_first_seg_index);
    RUN_TEST(bad_area_id_returns_new_asid);
    RUN_TEST(inactive_area_returns_new_asid);
    RUN_TEST(not_owner_returns_new_asid);
    RUN_TEST(shrink_resize_failure_returns_new_asid);
    RUN_TEST(grow_resize_failure_reverts_and_returns_new_asid);
    RUN_TEST(reversed_area_adds_the_segment_adjustment);
    RUN_TEST(relink_from_list_head);
    RUN_TEST(relink_from_list_middle);
    RUN_TEST(lock_sequence_on_success);
    RUN_TEST(waits_for_in_transition);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
