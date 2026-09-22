/*
 * ast/test/test_cond_flush.c - Unit tests for AST_$COND_FLUSH (0x00E05B9C)
 *
 * The test #includes ast/cond_flush.c directly and drives the real routine
 * through mocked callees.  It pins:
 *
 *   - the UID is copied to a local before the lookup (a different address
 *     with the same contents reaches ast_$lookup_aote_by_uid);
 *   - the DTV comparison is aote+0x38 (32 bits) then aote+0x3C (16 bits);
 *   - ast_$process_aote gets (purge = TRUE, keep = FALSE, wait = TRUE);
 *   - the AOTE is released only when processing succeeded, and the
 *     processing status is what the caller receives.
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

#include "ast/cond_flush.c"

static aote_t test_aote;
static uid_t  test_uid = { 0x11112222, 0x33334444 };

static int inhibit_begin_calls, inhibit_end_calls;
void PROC1_$INHIBIT_BEGIN(void) { inhibit_begin_calls++; }
void PROC1_$INHIBIT_END(void)   { inhibit_end_calls++; }

static int lock_calls, unlock_calls;
static int16_t last_lock_id;
void ML_$LOCK(int16_t id)   { lock_calls++; last_lock_id = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static uid_t  *lookup_arg;
static uid_t   lookup_arg_copy;
static aote_t *lookup_result;
aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    lookup_arg = uid;
    lookup_arg_copy = *uid;
    return lookup_result;
}

static int       process_calls;
static aote_t   *process_aote;
static boolean   process_f1, process_f2, process_f3;
static status_$t process_status;
uint16_t ast_$process_aote(aote_t *aote, boolean flags1, boolean flags2,
                           boolean flags3, status_$t *status)
{
    process_calls++;
    process_aote = aote;
    process_f1 = flags1; process_f2 = flags2; process_f3 = flags3;
    *status = process_status;
    return 0;
}

static int     release_calls;
static aote_t *release_aote;
void ast_$release_aote(aote_t *aote)
{
    release_calls++;
    release_aote = aote;
}

static void reset_state(void)
{
    memset(&test_aote, 0, sizeof(test_aote));
    test_aote.dtv_high = 0xAABBCCDD;
    test_aote.dtv_low = 0xEEFF;
    inhibit_begin_calls = inhibit_end_calls = 0;
    lock_calls = unlock_calls = 0;
    lookup_arg = NULL; memset(&lookup_arg_copy, 0, sizeof(lookup_arg_copy));
    lookup_result = NULL;
    process_calls = 0; process_aote = NULL; process_status = status_$ok;
    release_calls = 0; release_aote = NULL;
}

/* A clock_t with the given halves, handed over as the uint32_t * the
 * public prototype takes. */
static clock_t make_ts(uint32_t high, uint16_t low)
{
    clock_t c;
    c.high = high;
    c.low = low;
    return c;
}

TEST(not_active_leaves_status_ok)
{
    clock_t ts = make_ts(0xAABBCCDD, 0xEEFF);
    status_$t status = 0x12345678;

    AST_$COND_FLUSH(&test_uid, (uint32_t *)&ts, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, process_calls);
    ASSERT_EQ(1, inhibit_begin_calls);
    ASSERT_EQ(1, inhibit_end_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(AST_LOCK_ID, last_lock_id);
    ASSERT_EQ(1, unlock_calls);
    /* the lookup saw a copy, not the caller's cell */
    ASSERT_EQ(1, (uintptr_t)lookup_arg != (uintptr_t)&test_uid);
    ASSERT_EQ(0x11112222, lookup_arg_copy.high);
    ASSERT_EQ(0x33334444, lookup_arg_copy.low);
}

TEST(matching_dtv_not_flushed)
{
    clock_t ts = make_ts(0xAABBCCDD, 0xEEFF);
    status_$t status = 0;

    lookup_result = &test_aote;
    AST_$COND_FLUSH(&test_uid, (uint32_t *)&ts, &status);

    ASSERT_EQ(0, process_calls);
    ASSERT_EQ(0, release_calls);
    ASSERT_EQ(status_$ok, status);
}

TEST(high_word_differs_flushes_and_releases)
{
    clock_t ts = make_ts(0xAABBCCDE, 0xEEFF);
    status_$t status = 0;

    lookup_result = &test_aote;
    AST_$COND_FLUSH(&test_uid, (uint32_t *)&ts, &status);

    ASSERT_EQ(1, process_calls);
    ASSERT_EQ((uintptr_t)&test_aote, (uintptr_t)process_aote);
    ASSERT_EQ((uint8_t)-1, (uint8_t)process_f1);
    ASSERT_EQ(0, process_f2);
    ASSERT_EQ((uint8_t)-1, (uint8_t)process_f3);
    ASSERT_EQ(1, release_calls);
    ASSERT_EQ((uintptr_t)&test_aote, (uintptr_t)release_aote);
    ASSERT_EQ(status_$ok, status);
}

TEST(low_word_differs_flushes)
{
    clock_t ts = make_ts(0xAABBCCDD, 0xEEFE);
    status_$t status = 0;

    lookup_result = &test_aote;
    AST_$COND_FLUSH(&test_uid, (uint32_t *)&ts, &status);

    ASSERT_EQ(1, process_calls);
    ASSERT_EQ(1, release_calls);
}

TEST(process_failure_keeps_aote_and_status)
{
    clock_t ts = make_ts(0, 0);
    status_$t status = 0;

    lookup_result = &test_aote;
    process_status = status_$ast_segment_not_deactivatable;
    AST_$COND_FLUSH(&test_uid, (uint32_t *)&ts, &status);

    ASSERT_EQ(1, process_calls);
    ASSERT_EQ(0, release_calls);
    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(1, inhibit_end_calls);
}

int main(void)
{
    printf("test_cond_flush (AST_$COND_FLUSH 0x00E05B9C)\n");

    RUN_TEST(not_active_leaves_status_ok);
    RUN_TEST(matching_dtv_not_flushed);
    RUN_TEST(high_word_differs_flushes_and_releases);
    RUN_TEST(low_word_differs_flushes);
    RUN_TEST(process_failure_keeps_aote_and_status);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
