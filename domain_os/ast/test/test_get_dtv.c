/*
 * ast/test/test_get_dtv.c - Unit tests for AST_$GET_DTV (0x00E05476)
 *
 * The test #includes ast/get_dtv.c directly and drives the real routine
 * through mocked callees.  It pins:
 *
 *   - the UID is copied to a local before the lookup;
 *   - ast_$force_activate_segment gets the caller's location word and
 *     force = TRUE, and its status is what the caller sees on failure;
 *   - a resident AOTE is marked busy;
 *   - the 48-bit DTV is written as a longword then a word;
 *   - a remote object yields file_$object_not_found after the copy.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
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

#include "ast/get_dtv.c"

static aote_t test_aote;
static uid_t  test_uid = { 0x0000ABCD, 0x12345678 };

static int inhibit_begin_calls, inhibit_end_calls;
void PROC1_$INHIBIT_BEGIN(void) { inhibit_begin_calls++; }
void PROC1_$INHIBIT_END(void)   { inhibit_end_calls++; }

#define MAX_LOCKS 8
static int lock_calls, unlock_calls;
static int16_t lock_ids[MAX_LOCKS], unlock_ids[MAX_LOCKS];
void ML_$LOCK(int16_t id)   { if (lock_calls < MAX_LOCKS) lock_ids[lock_calls] = id; lock_calls++; }
void ML_$UNLOCK(int16_t id) { if (unlock_calls < MAX_LOCKS) unlock_ids[unlock_calls] = id; unlock_calls++; }

static uid_t  *lookup_arg;
static uid_t   lookup_copy;
static aote_t *lookup_result;
aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    lookup_arg = uid;
    lookup_copy = *uid;
    return lookup_result;
}

static int       force_calls;
static uint32_t  force_location;
static int8_t    force_flag;
static aote_t   *force_result;
static status_$t force_status;
aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t location,
                                    status_$t *status, int8_t force)
{
    (void)uid;
    force_calls++;
    force_location = location;
    force_flag = force;
    *status = force_status;
    return force_result;
}

static void reset_state(void)
{
    memset(&test_aote, 0, sizeof(test_aote));
    test_aote.dtv_high = 0x01020304;
    test_aote.dtv_low = 0x0506;
    inhibit_begin_calls = inhibit_end_calls = 0;
    lock_calls = unlock_calls = 0;
    lookup_arg = NULL; lookup_result = NULL;
    force_calls = 0; force_location = 0; force_flag = 0;
    force_result = NULL; force_status = status_$ok;
}

TEST(resident_local_object)
{
    uint32_t dtv[2] = { 0xEEEEEEEE, 0xEEEEEEEE };
    status_$t status = 0x77;

    lookup_result = &test_aote;
    AST_$GET_DTV(&test_uid, 0x12345, dtv, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x01020304, dtv[0]);
    ASSERT_EQ(0x0506, ((clock_t *)dtv)->low);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);
    ASSERT_EQ(0, force_calls);
    ASSERT_EQ(1, (uintptr_t)lookup_arg != (uintptr_t)&test_uid);
    ASSERT_EQ(0x12345678, lookup_copy.low);
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(AST_LOCK_ID, lock_ids[0]);
    ASSERT_EQ(PMAP_LOCK_ID, lock_ids[1]);
    ASSERT_EQ(PMAP_LOCK_ID, unlock_ids[0]);
    ASSERT_EQ(AST_LOCK_ID, unlock_ids[1]);
    ASSERT_EQ(1, inhibit_begin_calls);
    ASSERT_EQ(1, inhibit_end_calls);
}

TEST(activated_with_force)
{
    uint32_t dtv[2] = { 0, 0 };
    status_$t status = 0;

    force_result = &test_aote;
    AST_$GET_DTV(&test_uid, 0x80001234, dtv, &status);

    ASSERT_EQ(1, force_calls);
    ASSERT_EQ(0x80001234, force_location);
    ASSERT_EQ(0xFF, (uint8_t)force_flag);
    ASSERT_EQ(0, test_aote.flags);              /* not marked busy */
    ASSERT_EQ(0x01020304, dtv[0]);
    ASSERT_EQ(status_$ok, status);
}

TEST(activation_failure)
{
    uint32_t dtv[2] = { 0xEEEEEEEE, 0xEEEEEEEE };
    status_$t status = 0;

    force_status = status_$ast_incompatible_request;
    AST_$GET_DTV(&test_uid, 0, dtv, &status);

    ASSERT_EQ(status_$ast_incompatible_request, status);
    ASSERT_EQ(0xEEEEEEEE, dtv[0]);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(1, inhibit_end_calls);
}

TEST(remote_object_not_found_after_copy)
{
    uint32_t dtv[2] = { 0, 0 };
    status_$t status = 0;

    test_aote.remote_flag = (int8_t)0x80;
    lookup_result = &test_aote;
    AST_$GET_DTV(&test_uid, 0, dtv, &status);

    ASSERT_EQ(file_$object_not_found, status);
    ASSERT_EQ(0x01020304, dtv[0]);              /* still copied */
}

int main(void)
{
    printf("test_get_dtv (AST_$GET_DTV 0x00E05476)\n");

    RUN_TEST(resident_local_object);
    RUN_TEST(activated_with_force);
    RUN_TEST(activation_failure);
    RUN_TEST(remote_object_not_found_after_copy);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
