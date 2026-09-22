/*
 * ast/test/test_activate_and_wire.c - Unit tests for AST_$ACTIVATE_AND_WIRE
 *                                     (0x00E02FB8)
 *
 * The test #includes ast/activate_and_wire.c directly and drives the real
 * routine through mocked callees.  It pins:
 *
 *   - the lock is taken and released exactly once on every path;
 *   - *status is cleared before the lookups;
 *   - ast_$force_activate_segment is only called when the UID lookup
 *     fails, with location 0 and force 0;
 *   - ast_$lookup_or_create_aste is only called when the ASTE lookup fails;
 *   - the byte wire count goes up by one on success and not on failure.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
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

#include "ast/activate_and_wire.c"

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static aote_t test_aote;
static aste_t test_aste;
static uid_t  test_uid = { 0x12345678, 0x9ABCDEF0 };

static int lock_calls, unlock_calls;
static int16_t last_lock_id, last_unlock_id;
void ML_$LOCK(int16_t id)   { lock_calls++; last_lock_id = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; last_unlock_id = id; }

static int     lookup_aote_calls;
static aote_t *lookup_aote_result;
aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    lookup_aote_calls++;
    if (uid != &test_uid) { return NULL; }
    return lookup_aote_result;
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

static int     lookup_aste_calls;
static int16_t lookup_aste_seg;
static aste_t *lookup_aste_result;
aste_t *ast_$lookup_aste(aote_t *aote, int16_t segment)
{
    lookup_aste_calls++;
    lookup_aste_seg = segment;
    return aote == &test_aote ? lookup_aste_result : NULL;
}

static int       create_calls;
static uint16_t  create_seg;
static aste_t   *create_result;
static status_$t create_status;
aste_t *ast_$lookup_or_create_aste(aote_t *aote, uint16_t segment,
                                   status_$t *status)
{
    (void)aote;
    create_calls++;
    create_seg = segment;
    *status = create_status;
    return create_result;
}

static void reset_state(void)
{
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&test_aste, 0, sizeof(test_aste));
    test_aste.wire_count = 3;
    lock_calls = unlock_calls = 0;
    lookup_aote_calls = 0; lookup_aote_result = NULL;
    force_calls = 0; force_location = 0xFF; force_flag = -1;
    force_result = NULL; force_status = status_$ok;
    lookup_aste_calls = 0; lookup_aste_seg = -1; lookup_aste_result = NULL;
    create_calls = 0; create_seg = 0xFFFF; create_result = NULL;
    create_status = status_$ok;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

TEST(resident_aote_and_aste)
{
    status_$t status = 0x12345678;
    aste_t *r;

    lookup_aote_result = &test_aote;
    lookup_aste_result = &test_aste;

    r = AST_$ACTIVATE_AND_WIRE(&test_uid, 5, &status);

    ASSERT_EQ((uintptr_t)&test_aste, (uintptr_t)r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(4, test_aste.wire_count);
    ASSERT_EQ(0, force_calls);
    ASSERT_EQ(0, create_calls);
    ASSERT_EQ(5, lookup_aste_seg);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(AST_LOCK_ID, last_lock_id);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(AST_LOCK_ID, last_unlock_id);
}

TEST(aote_activated_when_missing)
{
    status_$t status = 0;
    aste_t *r;

    lookup_aote_result = NULL;
    force_result = &test_aote;
    lookup_aste_result = &test_aste;

    r = AST_$ACTIVATE_AND_WIRE(&test_uid, 2, &status);

    ASSERT_EQ((uintptr_t)&test_aste, (uintptr_t)r);
    ASSERT_EQ(1, force_calls);
    ASSERT_EQ(0, force_location);          /* clr.l (-0x4,A6) */
    ASSERT_EQ(0, force_flag);              /* clr.w -(SP) */
    ASSERT_EQ(4, test_aste.wire_count);
}

TEST(aote_activation_failure)
{
    status_$t status = 0;
    aste_t *r;

    lookup_aote_result = NULL;
    force_result = NULL;
    force_status = status_$ast_incompatible_request;

    r = AST_$ACTIVATE_AND_WIRE(&test_uid, 2, &status);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
    ASSERT_EQ(status_$ast_incompatible_request, status);
    ASSERT_EQ(0, lookup_aste_calls);
    ASSERT_EQ(0, create_calls);
    ASSERT_EQ(3, test_aste.wire_count);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
}

TEST(aste_created_when_missing)
{
    status_$t status = 0;
    aste_t *r;

    lookup_aote_result = &test_aote;
    lookup_aste_result = NULL;
    create_result = &test_aste;

    r = AST_$ACTIVATE_AND_WIRE(&test_uid, 9, &status);

    ASSERT_EQ((uintptr_t)&test_aste, (uintptr_t)r);
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(9, create_seg);
    ASSERT_EQ(4, test_aste.wire_count);
}

TEST(aste_creation_failure)
{
    status_$t status = 0;
    aste_t *r;

    lookup_aote_result = &test_aote;
    lookup_aste_result = NULL;
    create_result = NULL;
    create_status = status_$ast_segment_not_deactivatable;

    r = AST_$ACTIVATE_AND_WIRE(&test_uid, 9, &status);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(3, test_aste.wire_count);
    ASSERT_EQ(1, unlock_calls);
}

/* addq.b #1 wraps the byte */
TEST(wire_count_is_a_byte)
{
    status_$t status = 0;

    lookup_aote_result = &test_aote;
    lookup_aste_result = &test_aste;
    test_aste.wire_count = 0xFF;

    (void)AST_$ACTIVATE_AND_WIRE(&test_uid, 1, &status);

    ASSERT_EQ(0, test_aste.wire_count);
}

int main(void)
{
    printf("test_activate_and_wire (AST_$ACTIVATE_AND_WIRE 0x00E02FB8)\n");

    RUN_TEST(resident_aote_and_aste);
    RUN_TEST(aote_activated_when_missing);
    RUN_TEST(aote_activation_failure);
    RUN_TEST(aste_created_when_missing);
    RUN_TEST(aste_creation_failure);
    RUN_TEST(wire_count_is_a_byte);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
