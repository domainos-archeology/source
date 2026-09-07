/*
 * fim/test/test_init_asid.c - Unit tests for FIM_$INIT_ASID (0x00E0AA24)
 *
 * The real fim/init_asid.c is #included below.
 *
 * Covered:
 *   - the argument is a by-reference word (pea (0x96,A3) at 0x00E73314)
 *   - FIM_$CLEAR_TRACE_FAULT is called with that word (0x00E0AA34)
 *   - FIM_$QUIT_EC[as].value, the head longword of the 12-byte eventcount,
 *     is copied into FIM_$QUIT_VALUE[as] (0x00E0AA54)
 *   - the quit inhibit is set to 0xFF / true (st at 0x00E0AA60)
 *   - only the addressed AS is touched
 */

#include <stdio.h>
#include <string.h>

#include "fim/fim_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Globals and callees the code under test references
 * ============================================================================ */

uint32_t         FIM_$QUIT_VALUE[FIM_AS_COUNT];
ec_$eventcount_t FIM_$QUIT_EC[FIM_AS_COUNT];
int8_t           FIM_$QUIT_INH[FIM_AS_COUNT];

static int     clear_trace_calls;
static int16_t clear_trace_last_arg;

void FIM_$CLEAR_TRACE_FAULT(int16_t as_id)
{
    clear_trace_calls++;
    clear_trace_last_arg = as_id;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../init_asid.c"

/* ============================================================================
 * Tests
 * ============================================================================ */

static void reset_state(void)
{
    memset(FIM_$QUIT_VALUE, 0, sizeof(FIM_$QUIT_VALUE));
    memset(FIM_$QUIT_EC, 0, sizeof(FIM_$QUIT_EC));
    memset(FIM_$QUIT_INH, 0, sizeof(FIM_$QUIT_INH));
    clear_trace_calls = 0;
    clear_trace_last_arg = -1;
}

TEST(clears_the_trace_fault_for_the_referenced_as)
{
    int16_t as_id = 7;

    reset_state();
    FIM_$INIT_ASID(&as_id);

    /* 0x00E0AA30/0x00E0AA34: the word is dereferenced, then passed by value. */
    ASSERT_EQ(1, clear_trace_calls);
    ASSERT_EQ(7, clear_trace_last_arg);
    /* The argument itself is never written back. */
    ASSERT_EQ(7, as_id);
}

TEST(snapshots_the_quit_eventcount_value)
{
    int16_t as_id = 5;

    reset_state();
    FIM_$QUIT_EC[5].value = 0x12345678;
    FIM_$QUIT_EC[5].waiter_list_head = (ec_$eventcount_waiter_t *)0x11223344;
    FIM_$QUIT_EC[5].waiter_list_tail = (ec_$eventcount_waiter_t *)0x55667788;

    FIM_$INIT_ASID(&as_id);

    /* 0x00E0AA54 copies one longword: the value, not the waiter links. */
    ASSERT_EQ(0x12345678u, FIM_$QUIT_VALUE[5]);
}

TEST(negative_eventcount_values_are_copied_verbatim)
{
    int16_t as_id = 1;

    reset_state();
    FIM_$QUIT_EC[1].value = -1;

    FIM_$INIT_ASID(&as_id);

    ASSERT_EQ(0xFFFFFFFFu, FIM_$QUIT_VALUE[1]);
}

TEST(sets_the_quit_inhibit)
{
    int16_t as_id = 2;

    reset_state();
    FIM_$INIT_ASID(&as_id);

    /* "st" writes 0xFF; a Pascal boolean is true when negative. */
    ASSERT_EQ(-1, FIM_$QUIT_INH[2]);
    ASSERT_EQ(1, FIM_$QUIT_INH[2] < 0);
}

TEST(touches_only_the_addressed_as)
{
    int16_t as_id = 4;

    reset_state();
    FIM_$QUIT_EC[3].value = 0xAAAAAAAA;
    FIM_$QUIT_EC[4].value = 0xBBBBBBBB;
    FIM_$QUIT_EC[5].value = 0xCCCCCCCC;

    FIM_$INIT_ASID(&as_id);

    ASSERT_EQ(0xBBBBBBBBu, FIM_$QUIT_VALUE[4]);
    ASSERT_EQ(0u, FIM_$QUIT_VALUE[3]);
    ASSERT_EQ(0u, FIM_$QUIT_VALUE[5]);
    ASSERT_EQ(0, FIM_$QUIT_INH[3]);
    ASSERT_EQ(0, FIM_$QUIT_INH[5]);
}

TEST(works_at_both_ends_of_the_as_table)
{
    int16_t as_id;

    reset_state();

    as_id = 0;
    FIM_$QUIT_EC[0].value = 0x00000101;
    FIM_$INIT_ASID(&as_id);
    ASSERT_EQ(0x00000101u, FIM_$QUIT_VALUE[0]);
    ASSERT_EQ(-1, FIM_$QUIT_INH[0]);

    as_id = FIM_AS_COUNT - 1;
    FIM_$QUIT_EC[FIM_AS_COUNT - 1].value = 0x00000202;
    FIM_$INIT_ASID(&as_id);
    ASSERT_EQ(0x00000202u, FIM_$QUIT_VALUE[FIM_AS_COUNT - 1]);
    ASSERT_EQ(-1, FIM_$QUIT_INH[FIM_AS_COUNT - 1]);
    ASSERT_EQ(FIM_AS_COUNT - 1, clear_trace_last_arg);
}

int main(void)
{
    printf("test_init_asid:\n");

    RUN_TEST(clears_the_trace_fault_for_the_referenced_as);
    RUN_TEST(snapshots_the_quit_eventcount_value);
    RUN_TEST(negative_eventcount_values_are_copied_verbatim);
    RUN_TEST(sets_the_quit_inhibit);
    RUN_TEST(touches_only_the_addressed_as);
    RUN_TEST(works_at_both_ends_of_the_as_table);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
