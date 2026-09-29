/*
 * ast/test/test_lookup_aste.c - Unit tests for ast_$lookup_aste (0x00E0250C)
 *                                and AST_$LOCATE_ASTE (0x00E07050)
 *
 * The test #includes both .c files directly.  It pins:
 *
 *   - the ASTE list is descending by segment (aste+0x0C) and the walk
 *     stops (NULL) as soon as an entry at or below the wanted segment
 *     that is not it has been reached;
 *   - a matching entry in transition is waited for with the AOTE's
 *     ref_count held, and the walk restarts from the head;
 *   - LOCATE_ASTE's hint path checks index range, transition, segment,
 *     AREA flag, AOTE transition and UID, else falls back to the lookup.
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

#include "ast/lookup_aste.c"
#include "ast/locate_aste.c"

#define TEST_N_ASTES 8
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

static aote_t test_aote;

static int wait_calls;
static int ref_count_during_wait;
static aste_t *wait_clear;
void AST_$WAIT_FOR_AST_INTRANS(void)
{
    wait_calls++;
    ref_count_during_wait = test_aote.ref_count;
    if (wait_clear != NULL) {
        wait_clear->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
    }
}

static aote_t *lookup_result;
static int lookup_calls;
aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    (void)uid;
    lookup_calls++;
    return lookup_result;
}

static void reset_state(void)
{
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(&test_aote, 0, sizeof(test_aote));
    AST_$SIZE_AST = TEST_N_ASTES;
    wait_calls = 0; ref_count_during_wait = -1; wait_clear = NULL;
    lookup_result = NULL; lookup_calls = 0;
}

/* list: [0] seg 9 -> [1] seg 5 -> [2] seg 2 */
static void build_list(void)
{
    AST_$AOT.aste[0].segment = 9; AST_$AOT.aste[0].next = &AST_$AOT.aste[1];
    AST_$AOT.aste[1].segment = 5; AST_$AOT.aste[1].next = &AST_$AOT.aste[2];
    AST_$AOT.aste[2].segment = 2; AST_$AOT.aste[2].next = NULL;
    test_aote.aste_list = &AST_$AOT.aste[0];
    AST_$AOT.aste[0].aote = AST_$AOT.aste[1].aote = AST_$AOT.aste[2].aote = &test_aote;
}

TEST(lookup_finds_middle)
{
    build_list();
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[1], (uintptr_t)ast_$lookup_aste(&test_aote, 5));
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[2], (uintptr_t)ast_$lookup_aste(&test_aote, 2));
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[0], (uintptr_t)ast_$lookup_aste(&test_aote, 9));
}

TEST(lookup_stops_past_wanted_segment)
{
    build_list();
    /* 7 sits between 9 and 5: the walk stops at 5 */
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)ast_$lookup_aste(&test_aote, 7));
    /* 1 is below everything: stops at the tail entry */
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)ast_$lookup_aste(&test_aote, 1));
    /* 12 is above the head: stops at once */
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)ast_$lookup_aste(&test_aote, 12));
    ASSERT_EQ(0, wait_calls);
}

TEST(lookup_empty_list)
{
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)ast_$lookup_aste(&test_aote, 3));
}

TEST(lookup_waits_with_ref_held)
{
    aste_t *r;

    build_list();
    AST_$AOT.aste[1].flags = ASTE_FLAG_IN_TRANS;
    wait_clear = &AST_$AOT.aste[1];
    test_aote.ref_count = 2;

    r = ast_$lookup_aste(&test_aote, 5);

    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[1], (uintptr_t)r);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(3, ref_count_during_wait);
    ASSERT_EQ(2, test_aote.ref_count);
}

TEST(locate_hint_hit)
{
    locate_request_t req;
    aste_t *r;

    build_list();
    test_aote.uid.high = 0xAA; test_aote.uid.low = 0xBB;
    req.uid_high = 0xAA; req.uid_low = 0xBB;
    req.segment = 5;
    req.hint = 0xFE00 | 2;               /* index 2 = AST_$AOT.aste[1] */

    r = AST_$LOCATE_ASTE(&req);

    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[1], (uintptr_t)r);
    ASSERT_EQ(0, lookup_calls);
}

TEST(locate_hint_rejected_falls_back)
{
    locate_request_t req;
    aste_t *r;

    build_list();
    test_aote.uid.high = 0xAA; test_aote.uid.low = 0xBB;
    req.uid_high = 0xAA; req.uid_low = 0xBB;
    req.segment = 5;
    lookup_result = &test_aote;

    /* wrong segment in the hinted ASTE */
    req.hint = 1;                        /* AST_$AOT.aste[0], seg 9 */
    r = AST_$LOCATE_ASTE(&req);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[1], (uintptr_t)r);
    ASSERT_EQ(1, lookup_calls);

    /* AREA flag */
    AST_$AOT.aste[1].flags = ASTE_FLAG_AREA;
    req.hint = 2;
    r = AST_$LOCATE_ASTE(&req);
    ASSERT_EQ(2, lookup_calls);
    AST_$AOT.aste[1].flags = 0;

    /* AOTE in transition */
    test_aote.flags = AOTE_FLAG_IN_TRANS;
    r = AST_$LOCATE_ASTE(&req);
    ASSERT_EQ(3, lookup_calls);
    test_aote.flags = 0;

    /* UID mismatch */
    req.uid_low = 0xBC;
    r = AST_$LOCATE_ASTE(&req);
    ASSERT_EQ(4, lookup_calls);
    req.uid_low = 0xBB;

    /* hint out of range */
    req.hint = TEST_N_ASTES + 1;
    r = AST_$LOCATE_ASTE(&req);
    ASSERT_EQ(5, lookup_calls);

    /* hint zero */
    req.hint = 0x1E00;
    r = AST_$LOCATE_ASTE(&req);
    ASSERT_EQ(6, lookup_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[1], (uintptr_t)r);
}

TEST(locate_unknown_object)
{
    locate_request_t req;
    aste_t *r;

    memset(&req, 0, sizeof(req));
    req.segment = 5;
    lookup_result = NULL;

    r = AST_$LOCATE_ASTE(&req);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
    ASSERT_EQ(1, lookup_calls);
}

int main(void)
{
    printf("test_lookup_aste (ast_$lookup_aste 0x00E0250C, AST_$LOCATE_ASTE 0x00E07050)\n");

    RUN_TEST(lookup_finds_middle);
    RUN_TEST(lookup_stops_past_wanted_segment);
    RUN_TEST(lookup_empty_list);
    RUN_TEST(lookup_waits_with_ref_held);
    RUN_TEST(locate_hint_hit);
    RUN_TEST(locate_hint_rejected_falls_back);
    RUN_TEST(locate_unknown_object);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
