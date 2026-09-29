/*
 * ast/test/test_lookup_aote_by_uid.c - Unit tests for ast_$lookup_aote_by_uid
 *                                      (0x00E0209E)
 *
 * The test #includes ast/lookup_aote_by_uid.c directly (the earlier
 * version carried a private copy of the routine) and drives the real code
 * through a mocked UID_$HASH and AST_$WAIT_FOR_AST_INTRANS.  It pins:
 *
 *   - UID_$HASH receives the caller's UID and the 251-bucket size word;
 *   - the chain is walked from AST_$DATA.aoth[hash] comparing aote+0x10;
 *   - a match in transition is waited for and the walk restarts from the
 *     chain head (so an entry inserted at the head meanwhile is seen);
 *   - no match yields NULL.
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

#include "ast/lookup_aote_by_uid.c"

#define TEST_BUCKETS 251
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

static aote_t a1, a2, a3;

static uid_t   *hash_uid_arg;
static uint16_t hash_size_arg;
static uint32_t hash_result;
uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    hash_uid_arg = uid;
    hash_size_arg = *table_size;
    return hash_result;
}

static int wait_calls;
static aote_t *wait_clear;      /* the entry whose transition ends */
static aote_t *wait_insert;     /* an entry pushed on the head meanwhile */
void AST_$WAIT_FOR_AST_INTRANS(void)
{
    wait_calls++;
    if (wait_clear != NULL) {
        wait_clear->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    }
    if (wait_insert != NULL) {
        wait_insert->hash_next = AST_$DATA.aoth[hash_result];
        AST_$DATA.aoth[hash_result] = wait_insert;
        wait_insert = NULL;
    }
}

static void set_uid(aote_t *a, uint32_t high, uint32_t low)
{
    a->uid.high = high;
    a->uid.low = low;
}

static void reset_state(void)
{
    memset(AST_$DATA.aoth, 0, sizeof(AST_$DATA.aoth));
    memset(&a1, 0, sizeof(a1)); memset(&a2, 0, sizeof(a2)); memset(&a3, 0, sizeof(a3));
    hash_uid_arg = NULL; hash_size_arg = 0; hash_result = 5;
    wait_calls = 0; wait_clear = NULL; wait_insert = NULL;
}

TEST(empty_bucket_returns_null)
{
    uid_t uid = { 0x11, 0x22 };
    aote_t *r = ast_$lookup_aote_by_uid(&uid);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
    ASSERT_EQ((uintptr_t)&uid, (uintptr_t)hash_uid_arg);
    ASSERT_EQ(0x00FB, hash_size_arg);
    ASSERT_EQ(0, wait_calls);
}

TEST(found_second_on_chain)
{
    uid_t uid = { 0x11, 0x22 };
    aote_t *r;

    set_uid(&a1, 0x11, 0x23);           /* low differs */
    set_uid(&a2, 0x11, 0x22);
    set_uid(&a3, 0x10, 0x22);
    AST_$DATA.aoth[5] = &a1; a1.hash_next = &a2; a2.hash_next = &a3;

    r = ast_$lookup_aote_by_uid(&uid);

    ASSERT_EQ((uintptr_t)&a2, (uintptr_t)r);
    ASSERT_EQ(0, wait_calls);
}

TEST(no_match_on_nonempty_chain)
{
    uid_t uid = { 0x99, 0x99 };
    aote_t *r;

    set_uid(&a1, 0x11, 0x22);
    AST_$DATA.aoth[5] = &a1;

    r = ast_$lookup_aote_by_uid(&uid);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
}

TEST(in_transition_waits_and_restarts_from_head)
{
    uid_t uid = { 0x11, 0x22 };
    aote_t *r;

    set_uid(&a1, 0x11, 0x22);
    a1.flags = AOTE_FLAG_IN_TRANS;
    AST_$DATA.aoth[5] = &a1;
    /* during the wait a fresh entry with the same UID lands on the head */
    set_uid(&a2, 0x11, 0x22);
    wait_clear = &a1;
    wait_insert = &a2;

    r = ast_$lookup_aote_by_uid(&uid);

    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ((uintptr_t)&a2, (uintptr_t)r);     /* the head, not a1 */
}

TEST(other_bucket_not_searched)
{
    uid_t uid = { 0x11, 0x22 };
    aote_t *r;

    set_uid(&a1, 0x11, 0x22);
    AST_$DATA.aoth[6] = &a1;                  /* hash says bucket 5 */

    r = ast_$lookup_aote_by_uid(&uid);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
}

int main(void)
{
    printf("test_lookup_aote_by_uid (ast_$lookup_aote_by_uid 0x00E0209E)\n");

    RUN_TEST(empty_bucket_returns_null);
    RUN_TEST(found_second_on_chain);
    RUN_TEST(no_match_on_nonempty_chain);
    RUN_TEST(in_transition_waits_and_restarts_from_head);
    RUN_TEST(other_bucket_not_searched);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
