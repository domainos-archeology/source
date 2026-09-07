/*
 * file/test/test_fork_lock.c - unit tests for FILE_$FORK_LOCK (0x00E74244)
 *
 * The real file/fork_lock.c is #included below, together with the real
 * file/file_data.c so the lock tables are the ones the subsystem actually
 * uses.  On a host build FILE_$LOT_BASE / FILE_$PROC_LOT_BASE resolve to
 * those C globals instead of the m68k absolute addresses, so the function can
 * be called directly (source-4jp).
 */

#include <stdio.h>
#include <string.h>

#include "file/file_internal.h"
#include "ml/ml.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-20s ... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Mocked globals and callees
 * ============================================================================ */

uint16_t PROC1_$AS_ID = 0;
uint32_t NODE_$ME = 0x00012345;

static int mock_lock_called;
static int mock_unlock_called;
static int16_t mock_lock_num;
static int16_t mock_unlock_num;

void ML_$LOCK(int16_t id)   { mock_lock_called++;   mock_lock_num = id; }
void ML_$UNLOCK(int16_t id) { mock_unlock_called++; mock_unlock_num = id; }

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../file_data.c"
#include "../fork_lock.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static void reset_world(int16_t parent_asid)
{
    memset(FILE_$LOCK_ENTRIES, 0,
           sizeof(file_lock_entry_t) * FILE_LOCK_ENTRY_COUNT);
    memset(FILE_$LOCK_TABLE, 0,
           sizeof(file_lock_table_entry_t) * FILE_LOCK_TABLE_ENTRIES);
    memset(FILE_$LOCK_TABLE2, 0,
           sizeof(uint16_t) * FILE_LOCK_TABLE_ENTRIES);

    mock_lock_called   = 0;
    mock_unlock_called = 0;
    mock_lock_num      = -1;
    mock_unlock_num    = -1;

    PROC1_$AS_ID = (uint16_t)parent_asid;
}

#define SLOT(asid, n)   FILE_$PROC_LOT_SLOT(asid, n)
#define COUNT(asid)     FILE_$PROC_LOT_COUNT(asid)
#define REFCNT(entry)   (FILE_$LOT_ENTRY(entry)->refcount)

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(no_locks)
{
    uint16_t child_asid = 1;
    status_$t status = 0xFFFFFFFF;

    reset_world(0);
    COUNT(0) = 0;

    FILE_$FORK_LOCK(&child_asid, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, COUNT(1));
    ASSERT_EQ(1, mock_lock_called);
    ASSERT_EQ(1, mock_unlock_called);
    ASSERT_EQ(FILE_LOT_ML_LOCK_ID, mock_lock_num);
    ASSERT_EQ(FILE_LOT_ML_LOCK_ID, mock_unlock_num);
}

TEST(one_entry)
{
    uint16_t child_asid = 2;
    status_$t status;

    reset_world(0);
    COUNT(0)   = 1;
    SLOT(0, 1) = 3;
    REFCNT(3)  = 1;

    FILE_$FORK_LOCK(&child_asid, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, COUNT(2));
    ASSERT_EQ(3, SLOT(2, 1));
    ASSERT_EQ(2, REFCNT(3));
}

TEST(sparse_entries)
{
    uint16_t child_asid = 3;
    status_$t status;

    reset_world(1);
    COUNT(1)   = 5;
    SLOT(1, 1) = 2;
    SLOT(1, 2) = 0;
    SLOT(1, 3) = 5;
    SLOT(1, 4) = 0;
    SLOT(1, 5) = 7;
    REFCNT(2) = 1;
    REFCNT(5) = 3;
    REFCNT(7) = 1;

    FILE_$FORK_LOCK(&child_asid, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(5, COUNT(3));
    ASSERT_EQ(2, SLOT(3, 1));
    ASSERT_EQ(5, SLOT(3, 3));
    ASSERT_EQ(7, SLOT(3, 5));
    ASSERT_EQ(0, SLOT(3, 2));
    ASSERT_EQ(0, SLOT(3, 4));
    ASSERT_EQ(2, REFCNT(2));
    ASSERT_EQ(4, REFCNT(5));
    ASSERT_EQ(2, REFCNT(7));
}

TEST(all_empty_slots)
{
    uint16_t child_asid = 2;
    status_$t status;

    reset_world(0);
    COUNT(0) = 3;

    FILE_$FORK_LOCK(&child_asid, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(3, COUNT(2));       /* the count is copied regardless */
    ASSERT_EQ(0, SLOT(2, 1));
    ASSERT_EQ(0, SLOT(2, 2));
    ASSERT_EQ(0, SLOT(2, 3));
}

TEST(shared_entry)
{
    uint16_t child_asid = 2;
    status_$t status;

    reset_world(0);
    COUNT(0)   = 3;
    SLOT(0, 1) = 4;
    SLOT(0, 2) = 4;
    SLOT(0, 3) = 4;
    REFCNT(4)  = 3;

    FILE_$FORK_LOCK(&child_asid, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(3, COUNT(2));
    ASSERT_EQ(4, SLOT(2, 1));
    ASSERT_EQ(4, SLOT(2, 2));
    ASSERT_EQ(4, SLOT(2, 3));
    ASSERT_EQ(6, REFCNT(4));      /* one increment per slot reference */
}

TEST(last_slot_is_included)
{
    uint16_t child_asid = 2;
    status_$t status;

    reset_world(0);
    /* `subq.w #1,D0w` + `dbf` at 0x00E7428C/0x00E742DE runs the body exactly
     * COUNT times, i.e. slots 1..COUNT inclusive. */
    COUNT(0)   = 2;
    SLOT(0, 1) = 8;
    SLOT(0, 2) = 9;
    REFCNT(8) = 1;
    REFCNT(9) = 1;

    FILE_$FORK_LOCK(&child_asid, &status);

    ASSERT_EQ(8, SLOT(2, 1));
    ASSERT_EQ(9, SLOT(2, 2));
    ASSERT_EQ(2, REFCNT(8));
    ASSERT_EQ(2, REFCNT(9));
    ASSERT_EQ(0, SLOT(2, 3));     /* slot COUNT+1 untouched */
}

TEST(child_asid_is_reread_through_the_pointer)
{
    uint16_t child_asid = 2;
    status_$t status;

    reset_world(0);
    COUNT(0)   = 1;
    SLOT(0, 1) = 6;
    REFCNT(6)  = 1;

    FILE_$FORK_LOCK(&child_asid, &status);

    /* 0x00E742C2 loads the ASID from the var parameter inside the loop and
     * 0x00E742EA loads it again for the count copy. */
    ASSERT_EQ(6, SLOT(2, 1));
    ASSERT_EQ(1, COUNT(2));
}

int main(void)
{
    printf("FILE_$FORK_LOCK (0x00E74244) tests\n");
    RUN_TEST(no_locks);
    RUN_TEST(one_entry);
    RUN_TEST(sparse_entries);
    RUN_TEST(all_empty_slots);
    RUN_TEST(shared_entry);
    RUN_TEST(last_slot_is_included);
    RUN_TEST(child_asid_is_reread_through_the_pointer);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
