/*
 * fim/test/test_free_pid.c - Unit tests for FIM_$FREE_PID (0x00E0AA6C)
 *
 * The real fim/free_pid.c is #included below.
 *
 * Covered:
 *   - the argument is a by-reference word (pea (-0xb0,A6) at 0x00E749DA)
 *   - FIM_$CLEAR_TRACE_FAULT is called with that word (0x00E0AA84)
 *   - FIM_$USER_FIM_ADDR[as] is cleared (clr.l at 0x00E0AA8E), which is the
 *     FIM_DATA_BASE + 0x3C table FIM_$INSTALL writes
 *   - the quit inhibit is set to 0xFF / true (st at 0x00E0AA98)
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

void  *FIM_$USER_FIM_ADDR[FIM_AS_COUNT];
int8_t FIM_$QUIT_INH[FIM_AS_COUNT];

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

#include "../free_pid.c"

/* ============================================================================
 * Tests
 * ============================================================================ */

static void *handler_a = (void *)0x11110000;
static void *handler_b = (void *)0x22220000;

static void reset_state(void)
{
    memset(FIM_$USER_FIM_ADDR, 0, sizeof(FIM_$USER_FIM_ADDR));
    memset(FIM_$QUIT_INH, 0, sizeof(FIM_$QUIT_INH));
    clear_trace_calls = 0;
    clear_trace_last_arg = -1;
}

TEST(clears_the_trace_fault_for_the_referenced_as)
{
    int16_t as_id = 9;

    reset_state();
    FIM_$FREE_PID(&as_id);

    ASSERT_EQ(1, clear_trace_calls);
    ASSERT_EQ(9, clear_trace_last_arg);
    /* The argument itself is never written back. */
    ASSERT_EQ(9, as_id);
}

TEST(drops_the_user_fim_handler)
{
    int16_t as_id = 6;

    reset_state();
    FIM_$USER_FIM_ADDR[6] = handler_a;

    FIM_$FREE_PID(&as_id);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$USER_FIM_ADDR[6]);
}

TEST(sets_the_quit_inhibit)
{
    int16_t as_id = 6;

    reset_state();
    FIM_$FREE_PID(&as_id);

    /* "st" writes 0xFF; a Pascal boolean is true when negative. */
    ASSERT_EQ(-1, FIM_$QUIT_INH[6]);
    ASSERT_EQ(1, FIM_$QUIT_INH[6] < 0);
}

TEST(is_idempotent_when_no_handler_was_installed)
{
    int16_t as_id = 6;

    reset_state();
    FIM_$FREE_PID(&as_id);
    FIM_$FREE_PID(&as_id);

    ASSERT_EQ(2, clear_trace_calls);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$USER_FIM_ADDR[6]);
    ASSERT_EQ(-1, FIM_$QUIT_INH[6]);
}

TEST(touches_only_the_addressed_as)
{
    int16_t as_id = 6;

    reset_state();
    FIM_$USER_FIM_ADDR[5] = handler_a;
    FIM_$USER_FIM_ADDR[6] = handler_a;
    FIM_$USER_FIM_ADDR[7] = handler_b;

    FIM_$FREE_PID(&as_id);

    ASSERT_EQ((uintptr_t)handler_a, (uintptr_t)FIM_$USER_FIM_ADDR[5]);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$USER_FIM_ADDR[6]);
    ASSERT_EQ((uintptr_t)handler_b, (uintptr_t)FIM_$USER_FIM_ADDR[7]);
    ASSERT_EQ(0, FIM_$QUIT_INH[5]);
    ASSERT_EQ(0, FIM_$QUIT_INH[7]);
}

TEST(works_at_both_ends_of_the_as_table)
{
    int16_t as_id;

    reset_state();
    FIM_$USER_FIM_ADDR[0] = handler_a;
    FIM_$USER_FIM_ADDR[FIM_AS_COUNT - 1] = handler_b;

    as_id = 0;
    FIM_$FREE_PID(&as_id);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$USER_FIM_ADDR[0]);
    ASSERT_EQ(-1, FIM_$QUIT_INH[0]);

    as_id = FIM_AS_COUNT - 1;
    FIM_$FREE_PID(&as_id);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$USER_FIM_ADDR[FIM_AS_COUNT - 1]);
    ASSERT_EQ(-1, FIM_$QUIT_INH[FIM_AS_COUNT - 1]);
    ASSERT_EQ(FIM_AS_COUNT - 1, clear_trace_last_arg);
}

int main(void)
{
    printf("test_free_pid:\n");

    RUN_TEST(clears_the_trace_fault_for_the_referenced_as);
    RUN_TEST(drops_the_user_fim_handler);
    RUN_TEST(sets_the_quit_inhibit);
    RUN_TEST(is_idempotent_when_no_handler_was_installed);
    RUN_TEST(touches_only_the_addressed_as);
    RUN_TEST(works_at_both_ends_of_the_as_table);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
