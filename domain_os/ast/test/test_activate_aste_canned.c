/*
 * ast/test/test_activate_aste_canned.c - Unit tests for
 *                                        AST_$ACTIVATE_ASTE_CANNED (0x00E2F1D4)
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>

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

#include "ast/activate_aste_canned.c"

MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);

static aote_t the_aote;
static aste_t new_aste, head_aste, second_aste, found_aste;

/* ---- mocks ---- */
static int lock_calls, unlock_calls, crash_calls;
static int lookup_calls, create_calls;
static aote_t *lookup_aote_ret;
static aste_t *lookup_aste_ret, *create_ret;
static status_$t create_status, crash_status;
static jmp_buf crash_jmp;

void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }
aote_t *ast_$lookup_aote_by_uid(uid_t *uid) { (void)uid; return lookup_aote_ret; }
aste_t *AST_$ALLOCATE_ASTE(void) { return &new_aste; }
aste_t *ast_$lookup_aste(aote_t *aote, int16_t segment)
{
    (void)aote; (void)segment; lookup_calls++; return lookup_aste_ret;
}
aste_t *ast_$lookup_or_create_aste(aote_t *aote, uint16_t segment, status_$t *status)
{
    (void)aote; (void)segment; create_calls++; *status = create_status;
    return create_ret;
}
void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++; crash_status = *status_p; longjmp(crash_jmp, 1);
}

static void reset_state(void)
{
    memset(&AST_$DATA, 0, sizeof(AST_$DATA));
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&the_aote, 0, sizeof(the_aote));
    memset(&new_aste, 0, sizeof(new_aste));
    memset(&head_aste, 0, sizeof(head_aste));
    memset(&second_aste, 0, sizeof(second_aste));
    memset(&found_aste, 0, sizeof(found_aste));
    lock_calls = unlock_calls = crash_calls = lookup_calls = create_calls = 0;
    lookup_aote_ret = &the_aote;
    lookup_aste_ret = NULL;
    create_ret = NULL;
    create_status = 0;
    new_aste.seg_index = 3;
    new_aste.flags = 0xF8FF;
}

static uid_t canned_uid = { 0x00123456, 0x789 };
static uid_t other_uid  = { 0x01000000, 0x1 };

TEST(canned_fresh_aste_empty_list)
{
    uint32_t *row;
    PMAP_SEGMAP_ROW(3)[5].flags = 0xFF;          /* entry 5 = 0xFF000000 */
    aste_t *a = AST_$ACTIVATE_ASTE_CANNED(&canned_uid, 7);
    ASSERT_EQ((uintptr_t)&new_aste, (uintptr_t)a);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x00FF, new_aste.flags);           /* bits 15..11 clear */
    ASSERT_EQ(1, AST_$ASTE_L_CNT);
    ASSERT_EQ(0, AST_$ASTE_R_CNT);
    ASSERT_EQ(0, new_aste.page_count);
    ASSERT_EQ(1, new_aste.wire_count);
    ASSERT_EQ((uintptr_t)&the_aote, (uintptr_t)new_aste.aote);
    ASSERT_EQ(7, new_aste.segment);
    ASSERT_EQ((uintptr_t)&new_aste, (uintptr_t)the_aote.aste_list);
    ASSERT_EQ(0, (uintptr_t)new_aste.next);
    ASSERT_EQ(1, the_aote.status_flags);
    row = (uint32_t *)(void *)PMAP_SEGMAP_ROW(3);
    ASSERT_EQ(0x007FFFFF, row[0]);
    ASSERT_EQ(0x007FFFFF, row[31]);
    ASSERT_EQ(0x007FFFFF, row[5]);
    ASSERT_EQ(0, ((uint32_t *)(void *)PMAP_SEGMAP_ROW(2))[31]);
}

TEST(canned_remote_counts_and_flag)
{
    the_aote.remote_flag = (int8_t)0x80;
    AST_$ACTIVATE_ASTE_CANNED(&canned_uid, 0);
    ASSERT_EQ(0x08FF, new_aste.flags);
    ASSERT_EQ(1, AST_$ASTE_R_CNT);
    ASSERT_EQ(0, AST_$ASTE_L_CNT);
}

TEST(canned_insert_above_head)
{
    /* seg > head->segment: `bls' 0x00E2F290 not taken, inserted at the head */
    head_aste.segment = 4;
    the_aote.aste_list = &head_aste;
    AST_$ACTIVATE_ASTE_CANNED(&canned_uid, 5);
    ASSERT_EQ((uintptr_t)&new_aste, (uintptr_t)the_aote.aste_list);
    ASSERT_EQ((uintptr_t)&head_aste, (uintptr_t)new_aste.next);
    ASSERT_EQ(0, crash_calls);
}

TEST(canned_equal_head_crashes)
{
    /* seg == head->segment enters the walk, whose first test is the
     * duplicate check (0x00E2F29C-0x00E2F2A0) */
    head_aste.segment = 4;
    the_aote.aste_list = &head_aste;
    if (setjmp(crash_jmp) == 0) {
        AST_$ACTIVATE_ASTE_CANNED(&canned_uid, 4);
        ASSERT_EQ(0, 1);
    }
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$ast_eof, crash_status);
}

TEST(canned_insert_after_head_with_successor)
{
    /* seg below the head's: the walk stops at the head because its
     * successor is not nil, even though the new segment is past the
     * successor's */
    head_aste.segment = 9;
    second_aste.segment = 2;
    head_aste.next = &second_aste;
    the_aote.aste_list = &head_aste;
    AST_$ACTIVATE_ASTE_CANNED(&canned_uid, 5);
    ASSERT_EQ((uintptr_t)&head_aste, (uintptr_t)the_aote.aste_list);
    ASSERT_EQ((uintptr_t)&new_aste, (uintptr_t)head_aste.next);
    ASSERT_EQ((uintptr_t)&second_aste, (uintptr_t)new_aste.next);
    ASSERT_EQ(0, crash_calls);
}

TEST(canned_equal_to_successor_not_checked)
{
    /* the duplicate test only ever sees the head (the walk stops there
     * while the head has a successor), so a segment equal to the
     * successor's is inserted without a crash */
    head_aste.segment = 9;
    second_aste.segment = 5;
    head_aste.next = &second_aste;
    the_aote.aste_list = &head_aste;
    AST_$ACTIVATE_ASTE_CANNED(&canned_uid, 5);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ((uintptr_t)&new_aste, (uintptr_t)head_aste.next);
}

TEST(no_aote_crashes)
{
    lookup_aote_ret = NULL;
    if (setjmp(crash_jmp) == 0) {
        AST_$ACTIVATE_ASTE_CANNED(&canned_uid, 0);
    }
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00030001, crash_status);
    ASSERT_EQ(0, unlock_calls);
}

TEST(other_uid_found)
{
    found_aste.wire_count = 2;
    lookup_aste_ret = &found_aste;
    aste_t *a = AST_$ACTIVATE_ASTE_CANNED(&other_uid, 3);
    ASSERT_EQ((uintptr_t)&found_aste, (uintptr_t)a);
    ASSERT_EQ(3, found_aste.wire_count);
    ASSERT_EQ(0, create_calls);
    ASSERT_EQ(1, unlock_calls);
}

TEST(other_uid_created)
{
    create_ret = &found_aste;
    aste_t *a = AST_$ACTIVATE_ASTE_CANNED(&other_uid, 3);
    ASSERT_EQ((uintptr_t)&found_aste, (uintptr_t)a);
    ASSERT_EQ(1, found_aste.wire_count);
    ASSERT_EQ(1, create_calls);
}

TEST(other_uid_create_fails)
{
    create_status = 0x00030006;
    if (setjmp(crash_jmp) == 0) {
        AST_$ACTIVATE_ASTE_CANNED(&other_uid, 3);
    }
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00030006, crash_status);
}

int main(void)
{
    printf("AST_$ACTIVATE_ASTE_CANNED tests\n");
    RUN_TEST(canned_fresh_aste_empty_list);
    RUN_TEST(canned_remote_counts_and_flag);
    RUN_TEST(canned_insert_above_head);
    RUN_TEST(canned_equal_head_crashes);
    RUN_TEST(canned_insert_after_head_with_successor);
    RUN_TEST(canned_equal_to_successor_not_checked);
    RUN_TEST(no_aote_crashes);
    RUN_TEST(other_uid_found);
    RUN_TEST(other_uid_created);
    RUN_TEST(other_uid_create_fails);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
