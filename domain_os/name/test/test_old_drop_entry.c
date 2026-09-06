/*
 * name/test/test_old_drop_entry.c - Unit tests for name_$old_drop_entry
 *
 * Tests the name-level directory entry drop function by mocking
 * the helper functions (name_$validate_leaf, NAME_$LOCK_DIR,
 * dir_$old_unlink_entry, NAME_$UNLOCK_DIR, ACL_$EXIT_SUPER).
 */

/* Suppress POSIX uid_t so we can define Apollo's uid_t struct */
#define uid_t posix_uid_t
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#undef uid_t

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

/* Apollo types */
typedef struct {
    uint32_t high;
    uint32_t low;
} uid_t;

typedef uint32_t status_$t;
#define status_$ok                     0
#define status_$naming_invalid_leaf    0xE000B

/*
 * Mock control variables
 */

/* name_$validate_leaf mock */
static int8_t mock_validate_result;
static uint16_t mock_validate_parsed_len;
static int mock_validate_called;

int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    mock_validate_called = 1;
    (void)name; (void)name_len;
    memset(parsed, 0, 32);
    if (mock_validate_result < 0) {
        /* Copy name to parsed to simulate validation */
        if (name_len <= 32) {
            memcpy(parsed, name, name_len);
        }
        *parsed_len = mock_validate_parsed_len;
    }
    return mock_validate_result;
}

/* NAME_$LOCK_DIR mock */
static status_$t mock_lock_status;
static uint32_t mock_lock_flags_received;
static uint32_t mock_lock_handle;
static int mock_lock_called;

void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    mock_lock_called = 1;
    /* Recombine the two words the way the original caller pushed them
     * (`move.l #imm,-(SP)`) so the existing assertions still read. */
    mock_lock_flags_received = ((uint32_t)(uint16_t)lock_mode << 16) |
                               (uint32_t)(uint16_t)acl_rights;
    *handle_ret = mock_lock_handle;
    *status_ret = mock_lock_status;
    (void)dir_uid;
}

/* dir_$old_unlink_entry mock */
static status_$t mock_unlink_status;
static uint16_t mock_unlink_op_type_received;
static uint32_t mock_unlink_handle_received;
static int mock_unlink_called;

void dir_$old_unlink_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                           uint16_t name_len, uint16_t op_type,
                           void *result, status_$t *status_ret)
{
    mock_unlink_called = 1;
    mock_unlink_handle_received = handle;
    mock_unlink_op_type_received = op_type;
    *status_ret = mock_unlink_status;
    (void)dir_uid; (void)name; (void)name_len; (void)result;
}

/* NAME_$UNLOCK_DIR mock */
static status_$t mock_unlock_status;
static int mock_unlock_called;

void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    mock_unlock_called = 1;
    *status_ret = mock_unlock_status;
}

/* ACL_$EXIT_SUPER mock */
static int mock_exit_super_called;

void ACL_$EXIT_SUPER(void)
{
    mock_exit_super_called = 1;
}

static void reset_mocks(void)
{
    mock_validate_result = 0;
    mock_validate_parsed_len = 0;
    mock_validate_called = 0;
    mock_lock_status = status_$ok;
    mock_lock_flags_received = 0;
    mock_lock_handle = 0;
    mock_lock_called = 0;
    mock_unlink_status = status_$ok;
    mock_unlink_op_type_received = 0;
    mock_unlink_handle_received = 0;
    mock_unlink_called = 0;
    mock_unlock_status = status_$ok;
    mock_unlock_called = 0;
    mock_exit_super_called = 0;
}

/*
 * Function under test - reimplemented here with the same logic as
 * the production code, to allow testing on the host platform.
 */
static void name_$old_drop_entry(uid_t *dir_uid, char *name, uint16_t name_len,
                                 uint16_t type, void *result, status_$t *status_ret)
{
    int8_t valid;
    uint16_t parsed_len;
    uint32_t handle;
    status_$t unlock_status;
    uint8_t parsed_name[32];

    valid = name_$validate_leaf(name, name_len, parsed_name, &parsed_len);
    if (valid < 0) {
        NAME_$LOCK_DIR(dir_uid, &handle, 4, (int16_t)type, status_ret);
        if (*status_ret == status_$ok) {
            dir_$old_unlink_entry(dir_uid, handle, parsed_name, parsed_len,
                                  1, result, status_ret);
            NAME_$UNLOCK_DIR(&unlock_status);
            if ((*status_ret & 0xFFFF) == 0) {
                *status_ret = unlock_status;
            }
        }
        ACL_$EXIT_SUPER();
    } else {
        *status_ret = status_$naming_invalid_leaf;
    }
}

/* Test: invalid leaf name returns status_$naming_invalid_leaf */
TEST(invalid_leaf_returns_error)
{
    reset_mocks();
    mock_validate_result = 0;  /* validation fails */

    uid_t dir_uid = {0x1234, 0x5678};
    uint8_t result[8] = {0};
    status_$t status = 0;

    name_$old_drop_entry(&dir_uid, "bad\\name", 8, 0, result, &status);

    ASSERT_EQ(status_$naming_invalid_leaf, status);
    ASSERT_EQ(1, mock_validate_called);
    ASSERT_EQ(0, mock_lock_called);
    ASSERT_EQ(0, mock_exit_super_called);
}

/* Test: valid leaf, successful lock, unlink, and unlock */
TEST(successful_drop)
{
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;  /* valid */
    mock_validate_parsed_len = 4;
    mock_lock_status = status_$ok;
    mock_lock_handle = 0xDEAD0000;
    mock_unlink_status = status_$ok;
    mock_unlock_status = status_$ok;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uint8_t result[8] = {0};
    status_$t status = 0xFFFF;

    name_$old_drop_entry(&dir_uid, "test", 4, 0, result, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_validate_called);
    ASSERT_EQ(1, mock_lock_called);
    ASSERT_EQ(1, mock_unlink_called);
    ASSERT_EQ(1, mock_unlock_called);
    ASSERT_EQ(1, mock_exit_super_called);
    /* Verify op_type=1 passed to unlink */
    ASSERT_EQ(1, mock_unlink_op_type_received);
    /* Verify handle passed through */
    ASSERT_EQ(0xDEAD0000, mock_unlink_handle_received);
}

/* Test: lock flags are built correctly from type parameter */
TEST(lock_flags_construction)
{
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;
    mock_validate_parsed_len = 3;
    mock_lock_status = 0x12345678;  /* lock fails */

    uid_t dir_uid = {0, 0};
    uint8_t result[8] = {0};
    status_$t status = 0;

    /* Pass type=0 - flags should be (4 << 16) | 0 = 0x40000 */
    name_$old_drop_entry(&dir_uid, "abc", 3, 0, result, &status);
    ASSERT_EQ(0x40000, mock_lock_flags_received);

    /* Pass type=2 - flags should be (4 << 16) | 2 = 0x40002 */
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;
    mock_validate_parsed_len = 3;
    mock_lock_status = 0x12345678;
    name_$old_drop_entry(&dir_uid, "abc", 3, 2, result, &status);
    ASSERT_EQ(0x40002, mock_lock_flags_received);
}

/* Test: lock failure skips unlink/unlock, still calls EXIT_SUPER */
TEST(lock_failure_skips_unlink)
{
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;
    mock_validate_parsed_len = 4;
    mock_lock_status = 0x000E0016;  /* directory_locked error */

    uid_t dir_uid = {0, 0};
    uint8_t result[8] = {0};
    status_$t status = 0;

    name_$old_drop_entry(&dir_uid, "test", 4, 0, result, &status);

    ASSERT_EQ(0x000E0016, status);
    ASSERT_EQ(1, mock_lock_called);
    ASSERT_EQ(0, mock_unlink_called);
    ASSERT_EQ(0, mock_unlock_called);
    ASSERT_EQ(1, mock_exit_super_called);
}

/* Test: unlink error is preserved over unlock success */
TEST(unlink_error_preserved)
{
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;
    mock_validate_parsed_len = 4;
    mock_lock_status = status_$ok;
    mock_lock_handle = 0x1000;
    mock_unlink_status = 0x000E000A;  /* some naming error */
    mock_unlock_status = status_$ok;

    uid_t dir_uid = {0, 0};
    uint8_t result[8] = {0};
    status_$t status = 0;

    name_$old_drop_entry(&dir_uid, "test", 4, 0, result, &status);

    /* Unlink error has non-zero low word, so unlock status is NOT propagated */
    ASSERT_EQ(0x000E000A, status);
    ASSERT_EQ(1, mock_unlink_called);
    ASSERT_EQ(1, mock_unlock_called);
}

/* Test: unlock error propagated when unlink succeeds */
TEST(unlock_error_propagated)
{
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;
    mock_validate_parsed_len = 4;
    mock_lock_status = status_$ok;
    mock_lock_handle = 0x2000;
    mock_unlink_status = status_$ok;
    mock_unlock_status = 0x80000005;  /* unlock error */

    uid_t dir_uid = {0, 0};
    uint8_t result[8] = {0};
    status_$t status = 0;

    name_$old_drop_entry(&dir_uid, "test", 4, 0, result, &status);

    /* Unlink succeeded (status_$ok has low word 0), so unlock status is used */
    ASSERT_EQ(0x80000005, status);
}

/* Test: both unlink and unlock fail - unlink error kept (low word non-zero) */
TEST(both_fail_keeps_unlink_error)
{
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;
    mock_validate_parsed_len = 4;
    mock_lock_status = status_$ok;
    mock_lock_handle = 0x3000;
    mock_unlink_status = 0x000E0003;  /* name_already_exists */
    mock_unlock_status = 0x80000005;  /* unlock error */

    uid_t dir_uid = {0, 0};
    uint8_t result[8] = {0};
    status_$t status = 0;

    name_$old_drop_entry(&dir_uid, "test", 4, 0, result, &status);

    /* Unlink error low word is non-zero, so it's kept */
    ASSERT_EQ(0x000E0003, status);
}

/* Test: unlink error with zero low word gets unlock status */
TEST(unlink_subsystem_only_error_gets_unlock)
{
    reset_mocks();
    mock_validate_result = (int8_t)0xFF;
    mock_validate_parsed_len = 4;
    mock_lock_status = status_$ok;
    mock_lock_handle = 0x4000;
    mock_unlink_status = 0x000E0000;  /* subsystem code but zero error code */
    mock_unlock_status = 0x80000007;  /* unlock error */

    uid_t dir_uid = {0, 0};
    uint8_t result[8] = {0};
    status_$t status = 0;

    name_$old_drop_entry(&dir_uid, "test", 4, 0, result, &status);

    /* Low word of unlink status is 0, so unlock status is used */
    ASSERT_EQ(0x80000007, status);
}

int main(void)
{
    printf("name_$old_drop_entry tests:\n");

    RUN_TEST(invalid_leaf_returns_error);
    RUN_TEST(successful_drop);
    RUN_TEST(lock_flags_construction);
    RUN_TEST(lock_failure_skips_unlink);
    RUN_TEST(unlink_error_preserved);
    RUN_TEST(unlock_error_propagated);
    RUN_TEST(both_fail_keeps_unlink_error);
    RUN_TEST(unlink_subsystem_only_error_gets_unlock);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
