/*
 * file/test/test_lot_hash_modulus.c - the shared UID_$HASH modulus cell
 *
 * The image holds ONE word at 0x00E5EA28 (bytes 00 FB = 251) and reaches it
 * with `pea (d,PC)` from every FILE_ hashing site:
 *
 *   0x00E5E8FE  pea (0x128,PC)     FILE_$DELETE_INT       (source-ifam)
 *   0x00E60528  pea (-0x1b02,PC)   FILE_$LOCAL_READ_LOCK  (source-e05a)
 *   0x00E5F18C  pea (-0x766,PC)    FILE_$PRIV_LOCK
 *   0x00E5FD5A  pea (-0x1334,PC)   FILE_$PRIV_UNLOCK
 *
 * Before source-ifam / source-e05a the tree passed 58 from one site and NULL
 * from the other.  These tests pin the value AND the identity of the cell:
 * both functions must hand UID_$HASH the address of the same object, and a
 * remainder of 250 (the largest 251 can produce) must still index inside
 * FILE_$LOT_HASHTAB.
 *
 * The real file/file_data.c, file/delete_int.c and file/local_read_lock.c are
 * #included at the bottom; everything they call is mocked here.
 */

#include <stdio.h>
#include <string.h>

#include "file/file_internal.h"
#include "ml/ml.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_failed = 0;
static int tests_run = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-52s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        unsigned long _e = (unsigned long)(expected);                         \
        unsigned long _a = (unsigned long)(actual);                           \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected 0x%lx, got 0x%lx\n",              \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

#define ASSERT_PTR_EQ(expected, actual) do {                                  \
        const void *_e = (const void *)(expected);                            \
        const void *_a = (const void *)(actual);                              \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected %p, got %p\n", __LINE__, _e, _a); \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * Mocked globals
 * ============================================================================ */

uint32_t NODE_$ME  = 0x00012345;
uint32_t ROUTE_$PORT = 0x0000ABCD;
int8_t   AUDIT_$ENABLED = 0;
uint16_t PROC1_$AS_ID = 3;
uint16_t PROC1_$CURRENT = 1;

/* ============================================================================
 * Mock bookkeeping
 * ============================================================================ */

static int       mock_hash_calls;
static uid_t    *mock_hash_uid;
static uint16_t *mock_hash_modulus_ptr;
static uint16_t  mock_hash_modulus_value;
static uint32_t  mock_hash_result;

static int mock_ml_lock_calls;
static int mock_ml_unlock_calls;

/* ============================================================================
 * Mocks
 * ============================================================================ */

void ML_$LOCK(int16_t id)   { (void)id; mock_ml_lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; mock_ml_unlock_calls++; }

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    mock_hash_calls++;
    mock_hash_uid = uid;
    mock_hash_modulus_ptr = table_size;
    mock_hash_modulus_value = (table_size != NULL) ? *table_size : 0xDEAD;
    return mock_hash_result;
}

void FILE_$UID_LOCK_ACQUIRE(uid_t *uid) { (void)uid; }
void FILE_$UID_LOCK_RELEASE(uid_t *uid) { (void)uid; }

void FILE_$SET_ATTRIBUTE(uid_t *file_uid, int16_t attr_id, void *value,
                         uint16_t rights, int16_t options, status_$t *status)
{
    (void)file_uid; (void)attr_id; (void)value; (void)rights; (void)options;
    *status = status_$ok;
}

void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   uint8_t *result, status_$t *status)
{
    (void)uid; (void)new_size; (void)flags;
    *result = 0;
    *status = status_$ok;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../file_data.c"
#include "../delete_int.c"
#include "../local_read_lock.c"

static void reset_mocks(void)
{
    mock_hash_calls = 0;
    mock_hash_uid = NULL;
    mock_hash_modulus_ptr = NULL;
    mock_hash_modulus_value = 0;
    mock_hash_result = 0;
    mock_ml_lock_calls = 0;
    mock_ml_unlock_calls = 0;
    memset(FILE_$LOT_HASHTAB, 0, sizeof(FILE_$LOT_HASHTAB));
    memset(&FILE_$LOCK_CONTROL, 0, sizeof(FILE_$LOCK_CONTROL));
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* 0x00E5EA28: 00 FB */
TEST(cell_holds_251)
{
    ASSERT_EQ(251, file_$lot_hash_modulus);
    ASSERT_EQ(251, FILE_LOT_HASH_BUCKETS);
}

/* FILE_$LOCK_INIT clears 251 words at +0xC8, so both views hold 251 buckets. */
TEST(hash_tables_are_251_buckets)
{
    ASSERT_EQ(251, sizeof(FILE_$LOT_HASHTAB) / sizeof(FILE_$LOT_HASHTAB[0]));
    ASSERT_EQ(251, sizeof(FILE_$LOCK_CONTROL.lock_map) /
                   sizeof(FILE_$LOCK_CONTROL.lock_map[0]));
    ASSERT_EQ(0xC8, __builtin_offsetof(file_lock_control_t, lock_map));
}

/* 0x00E5E8FE: FILE_$DELETE_INT passes the cell, not a literal 58. */
TEST(delete_int_passes_the_modulus_cell)
{
    uid_t uid = { 0x11111111, 0x22222222 };
    uint8_t result = 0xAA;
    status_$t status = 0x1234;

    reset_mocks();
    mock_hash_result = 0;
    (void)FILE_$DELETE_INT(&uid, 0, &result, &status);

    ASSERT_EQ(1, mock_hash_calls);
    ASSERT_PTR_EQ(&uid, mock_hash_uid);
    ASSERT_PTR_EQ(&file_$lot_hash_modulus, mock_hash_modulus_ptr);
    ASSERT_EQ(251, mock_hash_modulus_value);
    /* 0x00E5E8FA clr.b (A0) then the tail unlocks ML lock 5 exactly once */
    ASSERT_EQ(0, result);
    ASSERT_EQ(1, mock_ml_lock_calls);
    ASSERT_EQ(1, mock_ml_unlock_calls);
}

/* 0x00E60528: FILE_$LOCAL_READ_LOCK passes the same cell, not NULL. */
TEST(local_read_lock_passes_the_modulus_cell)
{
    uid_t uid = { 0x33333333, 0x44444444 };
    file_lock_info_internal_t info;
    status_$t status = 0;

    reset_mocks();
    mock_hash_result = 0;
    memset(&info, 0, sizeof(info));
    FILE_$LOCAL_READ_LOCK(&uid, &info, &status);

    ASSERT_EQ(1, mock_hash_calls);
    ASSERT_PTR_EQ(&uid, mock_hash_uid);
    ASSERT_PTR_EQ(&file_$lot_hash_modulus, mock_hash_modulus_ptr);
    ASSERT_EQ(251, mock_hash_modulus_value);
    /* 0x00E6053A: the default status is written before the search */
    ASSERT_EQ(file_$object_not_locked_by_this_process, status);
}

/* Both sites must reach the SAME cell - the image has one word at 0xE5EA28. */
TEST(both_sites_share_one_cell)
{
    uid_t uid = { 1, 2 };
    uint8_t result = 0;
    status_$t status = 0;
    file_lock_info_internal_t info;
    uint16_t *from_delete;
    uint16_t *from_read;

    reset_mocks();
    (void)FILE_$DELETE_INT(&uid, 0, &result, &status);
    from_delete = mock_hash_modulus_ptr;

    reset_mocks();
    memset(&info, 0, sizeof(info));
    FILE_$LOCAL_READ_LOCK(&uid, &info, &status);
    from_read = mock_hash_modulus_ptr;

    ASSERT_PTR_EQ(from_delete, from_read);
}

/*
 * A modulus of 251 lets UID_$HASH return a remainder of 250; the bucket read
 * must stay inside the table.  Both routines read the head of that bucket and
 * find an empty chain.
 */
TEST(remainder_250_is_in_range)
{
    uid_t uid = { 5, 6 };
    uint8_t result = 0;
    status_$t status = 0;
    file_lock_info_internal_t info;

    reset_mocks();
    mock_hash_result = 250;
    FILE_$LOCK_CONTROL.lock_map[250] = 0;   /* empty chain */
    (void)FILE_$DELETE_INT(&uid, 0, &result, &status);
    ASSERT_EQ(status_$ok, status);

    reset_mocks();
    mock_hash_result = 250;
    FILE_$LOT_HASHTAB[250] = 0;             /* empty chain */
    memset(&info, 0, sizeof(info));
    FILE_$LOCAL_READ_LOCK(&uid, &info, &status);
    ASSERT_EQ(file_$object_not_locked_by_this_process, status);
}

int main(void)
{
    printf("FILE_ lock-hash modulus (0x00E5EA28) tests\n");
    RUN_TEST(cell_holds_251);
    RUN_TEST(hash_tables_are_251_buckets);
    RUN_TEST(delete_int_passes_the_modulus_cell);
    RUN_TEST(local_read_lock_passes_the_modulus_cell);
    RUN_TEST(both_sites_share_one_cell);
    RUN_TEST(remainder_250_is_in_range);
    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
