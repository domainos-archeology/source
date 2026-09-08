/*
 * dir/test/test_old_drop_linku.c - unit tests for DIR_$OLD_DROP_LINKU
 * (0x00E57924)
 *
 * Covers the two things bead source-us7e reported: the caller's target_uid is
 * handed to dir_$old_unlink_entry as its `result` argument (0x00E5797E), and
 * the unlock tail at 0x00E5799C-0x00E579AC replaces the caller's status only
 * when the caller's LOW WORD is still zero.  That is the second of the two
 * unlock-tail shapes in the OLD_DIR routines (the other, in
 * DIR_$OLD_CREATE_DIRU, replaces on a nonzero unlock status instead).
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int8_t vl_result;
int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    (void)name; (void)name_len;
    parsed[0] = 'L';
    *parsed_len = 1;
    return vl_result;
}

static int       lock_calls;
static int16_t   lock_mode_seen;
static int16_t   lock_rights_seen;
static status_$t lock_status;
void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    lock_calls++;
    lock_mode_seen = lock_mode;
    lock_rights_seen = acl_rights;
    (void)dir_uid;
    *handle_ret = 0x00040000u;
    *status_ret = lock_status;
}

static int       unlock_calls;
static status_$t unlock_status_out;
void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    unlock_calls++;
    *status_ret = unlock_status_out;
}

static int exit_super_calls;
void ACL_$EXIT_SUPER(void) { exit_super_calls++; }

static int       unlink_calls;
static uint16_t  unlink_op_seen;
static void     *unlink_result_seen;
static uint16_t  unlink_len_seen;
static status_$t unlink_status;
void dir_$old_unlink_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                           uint16_t name_len, uint16_t op_type,
                           void *result, status_$t *status_ret)
{
    unlink_calls++;
    unlink_op_seen = op_type;
    unlink_result_seen = result;
    unlink_len_seen = name_len;
    (void)dir_uid; (void)handle; (void)name;
    *status_ret = unlink_status;
}

#include "../old_drop_linku.c"

/* ------------------------------------------------------------------ */

static uid_t     dir = { 0x33333333u, 0x44444444u };
static uid_t     target;
static status_$t st;
static uint16_t  name_len;
static char      the_name[] = "LNK";

static void reset(void)
{
    vl_result = (int8_t)0xFF;
    lock_calls = 0; lock_status = status_$ok;
    lock_mode_seen = 0; lock_rights_seen = 0;
    unlock_calls = 0; unlock_status_out = status_$ok;
    exit_super_calls = 0;
    unlink_calls = 0; unlink_status = status_$ok;
    unlink_op_seen = 0xFFFF; unlink_result_seen = NULL; unlink_len_seen = 0xFFFF;
    target.high = 0; target.low = 0;
    st = 0x5A5A5A5A;
    name_len = 3;
}

TEST(an_invalid_leaf_returns_before_the_lock)
{
    reset();
    vl_result = 0;
    DIR_$OLD_DROP_LINKU(&dir, the_name, &name_len, &target, &st);
    ASSERT_EQ(status_$naming_invalid_leaf, st);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, exit_super_calls);
}

TEST(the_lock_takes_mode_4_and_rights_2)
{
    reset();
    DIR_$OLD_DROP_LINKU(&dir, the_name, &name_len, &target, &st);
    ASSERT_EQ(4, lock_mode_seen);
    ASSERT_EQ(2, lock_rights_seen);
}

/* 0x00E57978: tst.l (A3) - the WHOLE longword gates the unlink. */
TEST(a_failed_lock_skips_the_unlink)
{
    reset();
    lock_status = status_$naming_directory_locked;
    DIR_$OLD_DROP_LINKU(&dir, the_name, &name_len, &target, &st);
    ASSERT_EQ(0, unlink_calls);
    ASSERT_EQ(0, unlock_calls);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E5797E: the sixth argument is the caller's target_uid, not NULL. */
TEST(the_callers_target_uid_is_the_unlink_result)
{
    reset();
    DIR_$OLD_DROP_LINKU(&dir, the_name, &name_len, &target, &st);
    ASSERT_EQ(1, unlink_calls);
    ASSERT_EQ(3, unlink_op_seen);
    ASSERT_EQ(1, unlink_len_seen);       /* the PARSED length */
    ASSERT_TRUE(unlink_result_seen == (void *)&target);
}

/* 0x00E579A6: a zero status low word lets the unlock status through. */
TEST(the_unlock_status_replaces_a_zero_status)
{
    reset();
    unlink_status = status_$ok;
    unlock_status_out = status_$naming_internal_error;
    DIR_$OLD_DROP_LINKU(&dir, the_name, &name_len, &target, &st);
    ASSERT_EQ(status_$naming_internal_error, st);
}

/* A nonzero status low word keeps the body's status. */
TEST(a_nonzero_status_survives_the_unlock)
{
    reset();
    unlink_status = status_$naming_not_a_link;
    unlock_status_out = status_$naming_internal_error;
    DIR_$OLD_DROP_LINKU(&dir, the_name, &name_len, &target, &st);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(status_$naming_not_a_link, st);
    ASSERT_EQ(1, exit_super_calls);
}

/*
 * Only the LOW WORD is tested, so a status whose high half is set but whose
 * low half is zero is still overwritten.
 */
TEST(only_the_low_word_of_the_status_is_tested)
{
    reset();
    unlink_status = 0x00120000;
    unlock_status_out = status_$naming_internal_error;
    DIR_$OLD_DROP_LINKU(&dir, the_name, &name_len, &target, &st);
    ASSERT_EQ(status_$naming_internal_error, st);
}

int main(void)
{
    printf("DIR_$OLD_DROP_LINKU (0x00E57924) tests\n");

    RUN_TEST(an_invalid_leaf_returns_before_the_lock);
    RUN_TEST(the_lock_takes_mode_4_and_rights_2);
    RUN_TEST(a_failed_lock_skips_the_unlink);
    RUN_TEST(the_callers_target_uid_is_the_unlink_result);
    RUN_TEST(the_unlock_status_replaces_a_zero_status);
    RUN_TEST(a_nonzero_status_survives_the_unlock);
    RUN_TEST(only_the_low_word_of_the_status_is_tested);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
