/*
 * fim/test/test_get_user_pc.c - Unit tests for FIM_$GET_USER_PC (0x00E0AAA6)
 *
 * The real fim/get_user_pc.c is #included below.
 *
 * Covered:
 *   - the routine takes no arguments (link.w A6,-0x8 with nothing read from
 *     (0x8,A6)); it is called here as FIM_$GET_USER_PC()
 *   - PROC1_$GET_USP is called exactly once per call (jsr at 0x00E0AAAA)
 *   - the result is the longword at that address (move.l (A0),D0 at
 *     0x00E0AAB6), not the address itself and not a word or a byte
 *   - only the first longword of the user stack is read; the words on
 *     either side of it are ignored
 *   - the user stack is not written
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
 * Callees the code under test references
 * ============================================================================ */

/*
 * Stand-in user stack.  PROC1_$GET_USP (0x00E20F0C, "move USP,A0") hands
 * back the CPU's user stack pointer; here it hands back a pointer into this
 * array, whose element 0 is the slot the routine dereferences.
 */
static uint32_t fake_user_stack[4];
static uint32_t *fake_usp;
static int       get_usp_calls;

void *PROC1_$GET_USP(void)
{
    get_usp_calls++;
    return (void *)fake_usp;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../get_user_pc.c"

/* ============================================================================
 * Tests
 * ============================================================================ */

static void reset_state(void)
{
    memset(fake_user_stack, 0, sizeof(fake_user_stack));
    fake_usp = &fake_user_stack[0];
    get_usp_calls = 0;
}

TEST(returns_the_longword_the_usp_addresses)
{
    reset_state();
    fake_user_stack[0] = 0x00E0AAA6u;

    ASSERT_EQ(0x00E0AAA6u, FIM_$GET_USER_PC());
}

TEST(calls_get_usp_exactly_once)
{
    reset_state();
    fake_user_stack[0] = 0x12345678u;

    (void)FIM_$GET_USER_PC();
    ASSERT_EQ(1, get_usp_calls);

    (void)FIM_$GET_USER_PC();
    ASSERT_EQ(2, get_usp_calls);
}

TEST(returns_the_contents_not_the_pointer)
{
    reset_state();
    fake_user_stack[0] = 0u;

    /* A zero return proves the address itself was not returned. */
    ASSERT_EQ(0u, FIM_$GET_USER_PC());
    ASSERT_EQ(1, (uintptr_t)fake_usp != 0);
}

TEST(reads_a_full_longword)
{
    reset_state();

    /* Every byte of the longword participates in the result: a word or byte
     * read (or a byte-pointer cast) would drop part of this value. */
    fake_user_stack[0] = 0xFFFFFFFFu;
    ASSERT_EQ(0xFFFFFFFFu, FIM_$GET_USER_PC());

    fake_user_stack[0] = 0x0000FFFFu;
    ASSERT_EQ(0x0000FFFFu, FIM_$GET_USER_PC());

    fake_user_stack[0] = 0xFFFF0000u;
    ASSERT_EQ(0xFFFF0000u, FIM_$GET_USER_PC());
}

TEST(ignores_the_rest_of_the_user_stack)
{
    reset_state();
    fake_user_stack[0] = 0x0BADF00Du;
    fake_user_stack[1] = 0xDEADBEEFu;
    fake_user_stack[2] = 0xCAFEBABEu;
    fake_user_stack[3] = 0x5A5A5A5Au;

    ASSERT_EQ(0x0BADF00Du, FIM_$GET_USER_PC());
}

TEST(follows_the_usp_wherever_it_points)
{
    reset_state();
    fake_user_stack[0] = 0x11111111u;
    fake_user_stack[2] = 0x33333333u;

    /* A deeper stack (the process pushed more) still reads the top slot. */
    fake_usp = &fake_user_stack[2];
    ASSERT_EQ(0x33333333u, FIM_$GET_USER_PC());

    fake_usp = &fake_user_stack[0];
    ASSERT_EQ(0x11111111u, FIM_$GET_USER_PC());
}

TEST(does_not_write_the_user_stack)
{
    uint32_t before[4];

    reset_state();
    fake_user_stack[0] = 0x00E21828u;
    fake_user_stack[1] = 0x00E21878u;
    fake_user_stack[2] = 0x00E218CAu;
    fake_user_stack[3] = 0x00E218D0u;
    memcpy(before, fake_user_stack, sizeof(before));

    (void)FIM_$GET_USER_PC();

    ASSERT_EQ(0, memcmp(before, fake_user_stack, sizeof(before)));
}

int main(void)
{
    printf("test_get_user_pc:\n");

    RUN_TEST(returns_the_longword_the_usp_addresses);
    RUN_TEST(calls_get_usp_exactly_once);
    RUN_TEST(returns_the_contents_not_the_pointer);
    RUN_TEST(reads_a_full_longword);
    RUN_TEST(ignores_the_rest_of_the_user_stack);
    RUN_TEST(follows_the_usp_wherever_it_points);
    RUN_TEST(does_not_write_the_user_stack);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
