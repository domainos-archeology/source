/*
 * area/test/test_free_from.c - Unit tests for AREA_$FREE_FROM (0x00E07FC6)
 *
 * The test #includes area/free.c directly and drives the real
 * AREA_$FREE_FROM through mocked callees.  It pins down the behaviour
 * recovered from the disassembly in this pass:
 *
 *   - the bucket is M$OIU$WLW(remote_uid, 11) and the chain is searched by
 *     comparing hash->first_entry->remote_uid (entry+0x20) against the
 *     argument (0x00E08008-0x00E08010);
 *   - every entry on the record's list is deleted with
 *     area_$internal_delete(entry, entry->area_id, &status, FALSE) --
 *     the fourth argument is a zero WORD (`clr.w -(SP)` at 0x00E0802A);
 *   - the deleted entries go onto AREA_$FREE_LIST head-first and
 *     AREA_$N_FREE counts them (0x00E08050-0x00E0805E);
 *   - the hash record is unlinked from its bucket, using the saved
 *     predecessor when there is one (0x00E0806C-0x00E08078), and pushed
 *     onto AREA_$UID_HASH_FREE (0x00E0807A);
 *   - a UID that is not in the table leaves everything alone but still
 *     takes and releases lock 0x0E.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * Suppress area_internal.h (and with it as/ast/cal/ml/mmu/network/proc1/
 * rem_file/wp) so the test only pulls in the real area.h.
 */
#define AREA_INTERNAL_H
#define MISC_CRASH_SYSTEM_H
#define MATH_H

#include "area/area.h"

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
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
 * Module globals normally defined in area/area_data.c
 * ========================================================================== */

/* The whole AREA_ module data block is one object now (source-vm49);
 * the map-named cells are #define aliases onto its fields, so defining
 * AREA_$GLOBALS supplies every one of them. */
area_$globals_t AREA_$GLOBALS;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int      lock_calls;
static int      unlock_calls;
static int16_t  last_lock_id;
static int16_t  last_unlock_id;

void ML_$LOCK(int16_t resource_id)
{
    lock_calls++;
    last_lock_id = resource_id;
}

void ML_$UNLOCK(int16_t resource_id)
{
    unlock_calls++;
    last_unlock_id = resource_id;
}

#define MAX_DELETES 8
static int      delete_calls;
static area_$entry_t *deleted[MAX_DELETES];
static int16_t  deleted_area_id[MAX_DELETES];
static boolean  deleted_unlink[MAX_DELETES];
static status_$t mock_delete_status;

void area_$internal_delete(area_$entry_t *entry, int16_t area_id,
                           status_$t *status_p, boolean do_unlink)
{
    if (delete_calls < MAX_DELETES) {
        deleted[delete_calls] = entry;
        deleted_area_id[delete_calls] = area_id;
        deleted_unlink[delete_calls] = do_unlink;
    }
    delete_calls++;
    *status_p = mock_delete_status;
}

static int crash_calls;

void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    crash_calls++;
}

/* M$OIU$WLW is the compiler's signed 32/16 modulo helper. */
short M$OIU$WLW(long dividend, short divisor)
{
    return (short)(dividend % divisor);
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "area/free.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

#define REMOTE_UID  0x00000019u             /* 0x19 % 11 == 8 */
#define OTHER_UID   0x0000001Au

static area_$uid_hash_t hash_rec[4];
static area_$entry_t    entries[4];

static void reset_mocks(void)
{
    unsigned i;

    lock_calls = unlock_calls = 0;
    last_lock_id = last_unlock_id = -1;
    delete_calls = 0;
    crash_calls = 0;
    mock_delete_status = status_$ok;

    memset(deleted, 0, sizeof(deleted));
    memset(deleted_area_id, 0, sizeof(deleted_area_id));
    memset(deleted_unlink, 0, sizeof(deleted_unlink));
    memset(hash_rec, 0, sizeof(hash_rec));
    memset(entries, 0, sizeof(entries));

    for (i = 0; i < AREA_UID_HASH_BUCKETS; i++) {
        AREA_$UID_HASH[i] = NULL;
    }
    AREA_$UID_HASH_FREE = NULL;
    AREA_$FREE_LIST = NULL;
    AREA_$N_FREE = 0;
}

static int16_t bucket_of(uint32_t uid)
{
    return M$OIU$WLW((long)uid, AREA_UID_HASH_BUCKETS);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * A record whose entry list holds two areas: both are deleted, both land on
 * the free list (head-first, so the LAST one deleted ends up at the head),
 * AREA_$N_FREE counts them, the bucket is emptied and the record goes back
 * to the pool.
 */
TEST(frees_whole_chain_and_recycles_record)
{
    int16_t b = bucket_of(REMOTE_UID);

    entries[0].next = &entries[1];
    entries[0].remote_uid = REMOTE_UID;
    entries[0].area_id = 0x11;
    entries[1].next = NULL;
    entries[1].remote_uid = REMOTE_UID;
    entries[1].area_id = 0x22;

    hash_rec[0].next = NULL;
    hash_rec[0].first_entry = &entries[0];
    AREA_$UID_HASH[b] = &hash_rec[0];

    AREA_$FREE_FROM(REMOTE_UID);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(ML_LOCK_AREA, last_lock_id);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(ML_LOCK_AREA, last_unlock_id);

    ASSERT_EQ(2, delete_calls);
    ASSERT_TRUE(deleted[0] == &entries[0]);
    ASSERT_EQ(0x11, deleted_area_id[0]);
    ASSERT_EQ(0, deleted_unlink[0]);        /* clr.w -(SP) */
    ASSERT_TRUE(deleted[1] == &entries[1]);
    ASSERT_EQ(0x22, deleted_area_id[1]);

    /* head-first push: entries[1] was pushed last */
    ASSERT_TRUE(AREA_$FREE_LIST == &entries[1]);
    ASSERT_TRUE(entries[1].next == &entries[0]);
    ASSERT_TRUE(entries[1].prev == NULL);
    ASSERT_TRUE(entries[0].next == NULL);
    ASSERT_EQ(2, AREA_$N_FREE);

    ASSERT_TRUE(hash_rec[0].first_entry == NULL);
    ASSERT_TRUE(AREA_$UID_HASH[b] == NULL);
    ASSERT_TRUE(AREA_$UID_HASH_FREE == &hash_rec[0]);
    ASSERT_EQ(0, crash_calls);
}

/*
 * When the matching record is not the head of its bucket, the saved
 * predecessor is patched instead of the bucket head
 * (0x00E08076: `movea.l D3,A0` / `move.l (A3),(A0)`).
 */
TEST(unlinks_through_the_predecessor)
{
    int16_t b = bucket_of(REMOTE_UID);

    entries[0].next = NULL;
    entries[0].remote_uid = OTHER_UID;
    entries[1].next = NULL;
    entries[1].remote_uid = REMOTE_UID;

    hash_rec[0].first_entry = &entries[0];
    hash_rec[0].next = &hash_rec[1];
    hash_rec[1].first_entry = &entries[1];
    hash_rec[1].next = &hash_rec[2];
    hash_rec[2].first_entry = &entries[2];
    hash_rec[2].next = NULL;

    AREA_$UID_HASH[b] = &hash_rec[0];

    AREA_$FREE_FROM(REMOTE_UID);

    ASSERT_EQ(1, delete_calls);
    ASSERT_TRUE(deleted[0] == &entries[1]);

    ASSERT_TRUE(AREA_$UID_HASH[b] == &hash_rec[0]);
    ASSERT_TRUE(hash_rec[0].next == &hash_rec[2]);
    ASSERT_TRUE(AREA_$UID_HASH_FREE == &hash_rec[1]);
    ASSERT_TRUE(hash_rec[1].next == NULL);
}

/*
 * A UID that is in no bucket: the lock is still taken and released and
 * nothing else changes (0x00E0801C `beq.b 0x00e08082`).
 */
TEST(unknown_uid_is_a_no_op)
{
    int16_t b = bucket_of(REMOTE_UID);

    entries[0].remote_uid = OTHER_UID;
    hash_rec[0].first_entry = &entries[0];
    hash_rec[0].next = NULL;
    AREA_$UID_HASH[b] = &hash_rec[0];

    AREA_$FREE_FROM(REMOTE_UID);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, delete_calls);
    ASSERT_TRUE(AREA_$UID_HASH[b] == &hash_rec[0]);
    ASSERT_TRUE(AREA_$UID_HASH_FREE == NULL);
    ASSERT_EQ(0, AREA_$N_FREE);
}

/*
 * An empty bucket: the search terminates immediately without dereferencing
 * anything (0x00E08016 `cmpa.w #0x0,A3`).
 */
TEST(empty_bucket_is_a_no_op)
{
    AREA_$FREE_FROM(REMOTE_UID);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, delete_calls);
}

/*
 * A delete that reports a non-zero status crashes the system
 * (0x00E08044: `pea (-0x4,A6)` / `jsr 0x00e1e700.l`).
 */
TEST(delete_failure_crashes)
{
    int16_t b = bucket_of(REMOTE_UID);

    entries[0].next = NULL;
    entries[0].remote_uid = REMOTE_UID;
    hash_rec[0].first_entry = &entries[0];
    AREA_$UID_HASH[b] = &hash_rec[0];

    mock_delete_status = (status_$t)0x00030004;

    AREA_$FREE_FROM(REMOTE_UID);

    ASSERT_EQ(1, delete_calls);
    ASSERT_EQ(1, crash_calls);
}

int main(void)
{
    printf("AREA_$FREE_FROM tests\n");

    RUN_TEST(frees_whole_chain_and_recycles_record);
    RUN_TEST(unlinks_through_the_predecessor);
    RUN_TEST(unknown_uid_is_a_no_op);
    RUN_TEST(empty_bucket_is_a_no_op);
    RUN_TEST(delete_failure_crashes);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
