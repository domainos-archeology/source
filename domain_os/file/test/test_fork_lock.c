/*
 * Unit tests for FILE_$FORK_LOCK
 *
 * Tests the fork lock duplication logic which copies a parent process's
 * file lock table entries to a child process and increments reference
 * counts on each shared lock entry.
 *
 * Since we can't run actual kernel functions, we simulate the memory
 * layout with static arrays and verify the behavior.
 */

#include "base/base.h"

/*
 * Simulated memory layout for testing.
 *
 * The real kernel uses absolute addresses:
 *   - Per-process lock table: 0xE9F9CA + ASID*300 + slot*2
 *   - Lock entry refcounts:   0xE935BC + entry*0x1C + 0x0C
 *   - Slot counts:            0xEA3DC4 + ASID*2
 *
 * For testing, we use small arrays and verify the algorithm directly.
 */

#define TEST_MAX_ASID       4
#define TEST_MAX_SLOTS      10
#define TEST_MAX_ENTRIES    16
#define TEST_ENTRY_SIZE     0x1C  /* 28 bytes */

/* Simulated per-process lock table: slot_table[asid][slot] */
static uint16_t slot_table[TEST_MAX_ASID][TEST_MAX_SLOTS + 1]; /* slots 0..MAX */

/* Simulated slot counts per ASID */
static int16_t slot_count[TEST_MAX_ASID];

/* Simulated lock entry data (refcount at offset 0x0C) */
static uint8_t entry_data[TEST_MAX_ENTRIES][TEST_ENTRY_SIZE];

/* Mock ML_$LOCK / ML_$UNLOCK tracking */
static int mock_lock_called;
static int mock_unlock_called;
static int16_t mock_lock_num;
static int16_t mock_unlock_num;

/* Mock PROC1_$AS_ID */
static int16_t mock_parent_asid;

static void reset_mocks(void) {
    int i, j;
    mock_lock_called = 0;
    mock_unlock_called = 0;
    mock_lock_num = -1;
    mock_unlock_num = -1;
    mock_parent_asid = 0;

    for (i = 0; i < TEST_MAX_ASID; i++) {
        slot_count[i] = 0;
        for (j = 0; j <= TEST_MAX_SLOTS; j++) {
            slot_table[i][j] = 0;
        }
    }
    for (i = 0; i < TEST_MAX_ENTRIES; i++) {
        for (j = 0; j < TEST_ENTRY_SIZE; j++) {
            entry_data[i][j] = 0;
        }
    }
}

/*
 * Simplified version of FILE_$FORK_LOCK algorithm for testing.
 * Uses the simulated arrays instead of absolute memory addresses.
 */
static void fork_lock_sim(uint16_t *new_asid, status_$t *status_ret) {
    int16_t parent_asid = mock_parent_asid;
    int16_t child_asid;
    int16_t count;
    int16_t i;
    uint16_t entry_idx;

    *status_ret = status_$ok;

    mock_lock_called++;
    mock_lock_num = 5;

    count = slot_count[parent_asid];

    if (count != 0) {
        child_asid = *new_asid;

        for (i = 1; i <= count; i++) {
            entry_idx = slot_table[parent_asid][i];
            if (entry_idx != 0) {
                slot_table[child_asid][i] = entry_idx;
                entry_data[entry_idx][0x0C]++;
            }
        }
    }

    child_asid = *new_asid;
    slot_count[child_asid] = slot_count[parent_asid];

    mock_unlock_called++;
    mock_unlock_num = 5;
}

/*
 * Test: Fork with no locks (slot count = 0)
 * Expected: Child gets slot count 0, no entries copied, status OK
 */
void test_fork_lock_no_locks(void) {
    uint16_t child_asid = 1;
    status_$t status = 0xFFFFFFFF;

    reset_mocks();
    mock_parent_asid = 0;
    slot_count[0] = 0;

    fork_lock_sim(&child_asid, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(slot_count[1], 0);
    ASSERT_EQ(mock_lock_called, 1);
    ASSERT_EQ(mock_unlock_called, 1);
    ASSERT_EQ(mock_lock_num, 5);
    ASSERT_EQ(mock_unlock_num, 5);
}

/*
 * Test: Fork with one lock entry
 * Expected: Child gets same entry at same slot, refcount incremented
 */
void test_fork_lock_one_entry(void) {
    uint16_t child_asid = 2;
    status_$t status;

    reset_mocks();
    mock_parent_asid = 0;

    /* Parent has 1 slot, entry index 3 at slot 1 */
    slot_count[0] = 1;
    slot_table[0][1] = 3;
    entry_data[3][0x0C] = 1;  /* Initial refcount = 1 */

    fork_lock_sim(&child_asid, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(slot_count[2], 1);
    ASSERT_EQ(slot_table[2][1], 3);       /* Same entry index */
    ASSERT_EQ(entry_data[3][0x0C], 2);    /* Refcount incremented to 2 */
}

/*
 * Test: Fork with multiple entries, some slots empty
 * Expected: Only non-zero slots are copied, refcounts incremented
 */
void test_fork_lock_sparse_entries(void) {
    uint16_t child_asid = 3;
    status_$t status;

    reset_mocks();
    mock_parent_asid = 1;

    /* Parent has 5 slots: entries at slots 1, 3, 5; slots 2, 4 empty */
    slot_count[1] = 5;
    slot_table[1][1] = 2;
    slot_table[1][2] = 0;  /* empty */
    slot_table[1][3] = 5;
    slot_table[1][4] = 0;  /* empty */
    slot_table[1][5] = 7;

    entry_data[2][0x0C] = 1;
    entry_data[5][0x0C] = 3;
    entry_data[7][0x0C] = 1;

    fork_lock_sim(&child_asid, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(slot_count[3], 5);

    /* Non-zero entries copied */
    ASSERT_EQ(slot_table[3][1], 2);
    ASSERT_EQ(slot_table[3][3], 5);
    ASSERT_EQ(slot_table[3][5], 7);

    /* Empty slots remain empty */
    ASSERT_EQ(slot_table[3][2], 0);
    ASSERT_EQ(slot_table[3][4], 0);

    /* Refcounts incremented */
    ASSERT_EQ(entry_data[2][0x0C], 2);
    ASSERT_EQ(entry_data[5][0x0C], 4);
    ASSERT_EQ(entry_data[7][0x0C], 2);
}

/*
 * Test: Fork copies slot count even when all slots are empty
 * Expected: Slot count is copied, no entries modified
 */
void test_fork_lock_all_empty_slots(void) {
    uint16_t child_asid = 2;
    status_$t status;

    reset_mocks();
    mock_parent_asid = 0;

    /* Parent has slot count 3 but all entries are zero (freed) */
    slot_count[0] = 3;
    slot_table[0][1] = 0;
    slot_table[0][2] = 0;
    slot_table[0][3] = 0;

    fork_lock_sim(&child_asid, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(slot_count[2], 3);  /* Count still copied */

    /* No entries should have been touched */
    ASSERT_EQ(slot_table[2][1], 0);
    ASSERT_EQ(slot_table[2][2], 0);
    ASSERT_EQ(slot_table[2][3], 0);
}

/*
 * Test: Multiple entries sharing the same lock entry
 * Expected: Shared entry's refcount incremented once per reference
 */
void test_fork_lock_shared_entry(void) {
    uint16_t child_asid = 2;
    status_$t status;

    reset_mocks();
    mock_parent_asid = 0;

    /* Parent has 3 slots, all pointing to entry 4 */
    slot_count[0] = 3;
    slot_table[0][1] = 4;
    slot_table[0][2] = 4;
    slot_table[0][3] = 4;

    entry_data[4][0x0C] = 3;  /* Initial refcount = 3 */

    fork_lock_sim(&child_asid, &status);

    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(slot_count[2], 3);

    /* All slots copied */
    ASSERT_EQ(slot_table[2][1], 4);
    ASSERT_EQ(slot_table[2][2], 4);
    ASSERT_EQ(slot_table[2][3], 4);

    /* Refcount incremented 3 times (once per slot reference) */
    ASSERT_EQ(entry_data[4][0x0C], 6);
}
