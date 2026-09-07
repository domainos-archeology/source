/*
 * file/test/test_export_lk.c - unit tests for FILE_$EXPORT_LK (0x00E74110)
 *
 * The real file/export_lk.c is #included at the bottom together with
 * file/file_data.c.  PROC2_$FIND_ASID and ML_$LOCK / ML_$UNLOCK are mocked.
 *
 * These tests pin down bead source-xi4k: the lock-object table starts at
 * 0xE935CC, the reference count is the byte at entry+0x18 and the UID is at
 * entry+0x0C - and the constant cell at 0xE74242 that PROC2_$FIND_ASID
 * receives by reference holds 0xFF (TRUE), not zero.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-50s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "file/file_internal.h"
#include "proc2/proc2.h"

/* ------------------------------------------------------------------ */
/* Globals and mocks                                                    */
/* ------------------------------------------------------------------ */

uint16_t PROC1_$AS_ID = 3;
uint16_t PROC1_$CURRENT = 1;
uint32_t NODE_$ME = 0x00012345;

static int       m_find_calls;
static int8_t    m_find_flag_seen;
static uint16_t  m_find_result;
static status_$t m_find_status;

uint16_t PROC2_$FIND_ASID(uid_t *proc_uid, int8_t *param_2,
                          status_$t *status_ret)
{
    (void)proc_uid;
    m_find_calls++;
    m_find_flag_seen = *param_2;
    *status_ret = m_find_status;
    return m_find_result;
}

static int m_lock_calls, m_unlock_calls;
static int m_last_lock_id, m_last_unlock_id;

void ML_$LOCK(int16_t lock_id)   { m_lock_calls++;   m_last_lock_id = lock_id; }
void ML_$UNLOCK(int16_t lock_id) { m_unlock_calls++; m_last_unlock_id = lock_id; }

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

#define SRC_ASID    3
#define DST_ASID    9
#define SRC_SLOT    4
#define LOT_ENTRY   77

static uid_t     file_uid    = { 0x11112222u, 0x33334444u };
static uid_t     target_proc = { 0xAAAABBBBu, 0xCCCCDDDDu };
static uint32_t  lock_index;
static int32_t   index_out;
static status_$t status;

static void reset_world(void)
{
    memset(FILE_$LOCK_ENTRIES, 0,
           sizeof(file_lock_entry_t) * FILE_LOCK_ENTRY_COUNT);
    memset(FILE_$LOCK_TABLE, 0,
           sizeof(file_lock_table_entry_t) * FILE_LOCK_TABLE_ENTRIES);
    memset(FILE_$LOCK_TABLE2, 0,
           sizeof(uint16_t) * FILE_LOCK_TABLE_ENTRIES);

    PROC1_$AS_ID = SRC_ASID;
    lock_index = SRC_SLOT;
    index_out = -1;
    status = 0x7F7F7F7F;

    m_find_calls = 0;
    m_find_flag_seen = 0;
    m_find_result = DST_ASID;
    m_find_status = 0;
    m_lock_calls = m_unlock_calls = 0;
    m_last_lock_id = m_last_unlock_id = -1;
}

static void seed_held_lock(uid_t uid, uint8_t refcount)
{
    file_lock_entry_detail_t *e = FILE_$LOT_ENTRY(LOT_ENTRY);

    FILE_$PROC_LOT_SLOT(SRC_ASID, SRC_SLOT) = LOT_ENTRY;
    e->uid_high = uid.high;
    e->uid_low  = uid.low;
    e->refcount = refcount;
}

static void run(void)
{
    FILE_$EXPORT_LK(&file_uid, &lock_index, &target_proc, &index_out, &status);
}

/* ------------------------------------------------------------------ */

TEST(find_asid_receives_the_true_constant_from_0x00e74242)
{
    reset_world();
    seed_held_lock(file_uid, 1);
    run();
    ASSERT_EQ(1, m_find_calls);
    /* pea (0x112,PC) -> 0x00E74242, raw byte 0xFF. */
    ASSERT_EQ((int8_t)0xFF, m_find_flag_seen);
}

TEST(find_asid_failure_returns_immediately)
{
    reset_world();
    m_find_status = 0x00120001u;
    run();
    ASSERT_EQ(0x00120001u, status);
    ASSERT_EQ(0, m_lock_calls);
}

TEST(slot_zero_is_an_invalid_argument)
{
    reset_world();
    lock_index = 0;
    run();
    ASSERT_EQ(0x000F0014u, status);             /* 0x00E74154 */
}

TEST(slot_above_0x96_is_an_invalid_argument)
{
    reset_world();
    lock_index = 0x97;
    run();
    ASSERT_EQ(0x000F0014u, status);
}

TEST(slot_0x96_is_accepted)
{
    reset_world();
    lock_index = 0x96;
    /* `bls` at 0x00E74152 lets 0x96 through; the slot is empty, so the next
     * failure is "not locked", not "invalid argument". */
    run();
    ASSERT_EQ(0x000F0005u, status);
}

TEST(empty_slot_reports_not_locked_by_this_process)
{
    reset_world();
    run();
    ASSERT_EQ(0x000F0005u, status);             /* 0x00E741AE */
    ASSERT_EQ(0, m_lock_calls);
}

TEST(zero_refcount_reports_not_locked)
{
    reset_world();
    seed_held_lock(file_uid, 0);
    run();
    /* `move.b (-0x4,A3),D4b` / `tst.w` at 0x00E74194 - the byte at +0x18. */
    ASSERT_EQ(0x000F0005u, status);
}

TEST(foreign_uid_reports_not_locked)
{
    reset_world();
    uid_t other = { 0x99998888u, 0x77776666u };
    seed_held_lock(other, 1);
    run();
    ASSERT_EQ(0x000F0005u, status);
}

TEST(export_fills_the_first_free_slot_and_bumps_the_refcount)
{
    reset_world();
    seed_held_lock(file_uid, 1);
    run();
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, index_out);                    /* the first slot is free */
    ASSERT_EQ(LOT_ENTRY, FILE_$PROC_LOT_SLOT(DST_ASID, 1));
    ASSERT_EQ(2, FILE_$LOT_ENTRY(LOT_ENTRY)->refcount);
    ASSERT_EQ(1, FILE_$PROC_LOT_COUNT(DST_ASID));
    /* 0x00E741C4 / 0x00E74232 bracket the search with ML lock 5. */
    ASSERT_EQ(1, m_lock_calls);
    ASSERT_EQ(1, m_unlock_calls);
    ASSERT_EQ(5, m_last_lock_id);
    ASSERT_EQ(5, m_last_unlock_id);
}

TEST(export_skips_occupied_slots)
{
    reset_world();
    seed_held_lock(file_uid, 1);
    FILE_$PROC_LOT_SLOT(DST_ASID, 1) = 11;
    FILE_$PROC_LOT_SLOT(DST_ASID, 2) = 12;
    run();
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(3, index_out);
    ASSERT_EQ(LOT_ENTRY, FILE_$PROC_LOT_SLOT(DST_ASID, 3));
}

TEST(high_water_mark_only_grows)
{
    reset_world();
    seed_held_lock(file_uid, 1);
    FILE_$PROC_LOT_COUNT(DST_ASID) = 50;
    run();
    /* `cmp.w (0x1d98,A0),D4w` + `ble` at 0x00E7420C leaves the larger value. */
    ASSERT_EQ(50, FILE_$PROC_LOT_COUNT(DST_ASID));
}

TEST(a_full_target_table_reports_lock_table_full)
{
    reset_world();
    seed_held_lock(file_uid, 1);
    for (int i = 1; i <= 150; i++) {
        FILE_$PROC_LOT_SLOT(DST_ASID, i) = 5;
    }
    run();
    ASSERT_EQ(0x000F0009u, status);             /* 0x00E741B8, never cleared */
    ASSERT_EQ(-1, index_out);                   /* left untouched */
    ASSERT_EQ(1, FILE_$LOT_ENTRY(LOT_ENTRY)->refcount);
    ASSERT_EQ(1, m_unlock_calls);
}

TEST(the_search_covers_exactly_150_slots)
{
    reset_world();
    seed_held_lock(file_uid, 1);
    for (int i = 1; i <= 149; i++) {
        FILE_$PROC_LOT_SLOT(DST_ASID, i) = 5;
    }
    /* Slot 150 is the last the `move.w #0x95,D1w` + `dbf` loop reaches. */
    run();
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(150, index_out);
}

int main(void)
{
    printf("FILE_$EXPORT_LK (0x00E74110)\n");

    RUN_TEST(find_asid_receives_the_true_constant_from_0x00e74242);
    RUN_TEST(find_asid_failure_returns_immediately);
    RUN_TEST(slot_zero_is_an_invalid_argument);
    RUN_TEST(slot_above_0x96_is_an_invalid_argument);
    RUN_TEST(slot_0x96_is_accepted);
    RUN_TEST(empty_slot_reports_not_locked_by_this_process);
    RUN_TEST(zero_refcount_reports_not_locked);
    RUN_TEST(foreign_uid_reports_not_locked);
    RUN_TEST(export_fills_the_first_free_slot_and_bumps_the_refcount);
    RUN_TEST(export_skips_occupied_slots);
    RUN_TEST(high_water_mark_only_grows);
    RUN_TEST(a_full_target_table_reports_lock_table_full);
    RUN_TEST(the_search_covers_exactly_150_slots);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../export_lk.c"
#include "../file_data.c"
