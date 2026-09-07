/*
 * fim/test/test_install.c - Unit tests for FIM_$INSTALL (0x00E0A9C2) and
 * FIM_$GET_FIM_ADDR (0x00E0AA04)
 *
 * The real fim/install.c and fim/get_fim_addr.c are #included below.
 *
 * Covered:
 *   - FIM_$GET_FIM_ADDR returns FIM_$USER_FIM_ADDR for the current AS only
 *   - FIM_$INSTALL stores through its by-reference argument and returns the
 *     previous handler (0x00E0A9DC-0x00E0A9E2)
 *   - the quit inhibit is cleared only when there was no previous handler
 *     (`tst.l D1 / bne` at 0x00E0A9E6), and only for the current AS
 *   - installing over an existing handler leaves the inhibit alone
 *   - installing NULL is still a store, and the following install sees it
 *     as "no previous handler"
 */

#include <stdio.h>
#include <string.h>

#include "fim/fim_internal.h"
#include "proc1/proc1.h"

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
 * Globals the code under test references
 * ============================================================================ */

uint16_t PROC1_$AS_ID;
void    *FIM_$USER_FIM_ADDR[64];
int8_t   FIM_$QUIT_INH[64];

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../get_fim_addr.c"
#include "../install.c"

/* ============================================================================
 * Tests
 * ============================================================================ */

static void *handler_a = (void *)0x11110000;
static void *handler_b = (void *)0x22220000;

static void reset_state(void)
{
    memset(FIM_$USER_FIM_ADDR, 0, sizeof(FIM_$USER_FIM_ADDR));
    memset(FIM_$QUIT_INH, -1, sizeof(FIM_$QUIT_INH));
    PROC1_$AS_ID = 3;
}

TEST(get_returns_the_current_as_entry)
{
    reset_state();
    FIM_$USER_FIM_ADDR[3] = handler_a;
    FIM_$USER_FIM_ADDR[4] = handler_b;

    ASSERT_EQ((uintptr_t)handler_a, (uintptr_t)FIM_$GET_FIM_ADDR());

    PROC1_$AS_ID = 4;
    ASSERT_EQ((uintptr_t)handler_b, (uintptr_t)FIM_$GET_FIM_ADDR());
}

TEST(get_returns_null_when_nothing_installed)
{
    reset_state();
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$GET_FIM_ADDR());
}

TEST(first_install_stores_and_clears_the_quit_inhibit)
{
    void *new_addr = handler_a;
    void *old;

    reset_state();
    old = FIM_$INSTALL(&new_addr);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)old);
    ASSERT_EQ((uintptr_t)handler_a, (uintptr_t)FIM_$USER_FIM_ADDR[3]);
    ASSERT_EQ(0, FIM_$QUIT_INH[3]);
    /* Only the current AS is touched. */
    ASSERT_EQ(-1, FIM_$QUIT_INH[2]);
    ASSERT_EQ(-1, FIM_$QUIT_INH[4]);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$USER_FIM_ADDR[4]);
}

TEST(second_install_returns_the_previous_and_keeps_the_inhibit)
{
    void *new_addr = handler_b;
    void *old;

    reset_state();
    FIM_$USER_FIM_ADDR[3] = handler_a;

    old = FIM_$INSTALL(&new_addr);

    ASSERT_EQ((uintptr_t)handler_a, (uintptr_t)old);
    ASSERT_EQ((uintptr_t)handler_b, (uintptr_t)FIM_$USER_FIM_ADDR[3]);
    /* tst.l D1 / bne at 0x00E0A9E6: a non-nil previous handler skips the clr. */
    ASSERT_EQ(-1, FIM_$QUIT_INH[3]);
}

TEST(installing_null_still_stores_and_clears)
{
    void *new_addr = NULL;
    void *old;

    reset_state();
    old = FIM_$INSTALL(&new_addr);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)old);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)FIM_$USER_FIM_ADDR[3]);
    ASSERT_EQ(0, FIM_$QUIT_INH[3]);
}

TEST(uninstall_then_install_clears_again)
{
    void *addr;

    reset_state();
    FIM_$USER_FIM_ADDR[3] = handler_a;

    addr = NULL;
    (void)FIM_$INSTALL(&addr);          /* previous non-nil: inhibit kept */
    ASSERT_EQ(-1, FIM_$QUIT_INH[3]);

    addr = handler_b;
    (void)FIM_$INSTALL(&addr);          /* previous nil: inhibit cleared */
    ASSERT_EQ(0, FIM_$QUIT_INH[3]);
    ASSERT_EQ((uintptr_t)handler_b, (uintptr_t)FIM_$GET_FIM_ADDR());
}

int main(void)
{
    printf("test_install:\n");

    RUN_TEST(get_returns_the_current_as_entry);
    RUN_TEST(get_returns_null_when_nothing_installed);
    RUN_TEST(first_install_stores_and_clears_the_quit_inhibit);
    RUN_TEST(second_install_returns_the_previous_and_keeps_the_inhibit);
    RUN_TEST(installing_null_still_stores_and_clears);
    RUN_TEST(uninstall_then_install_clears_again);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
