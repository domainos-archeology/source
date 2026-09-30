/*
 * os/test/test_vtop_or_crash.c - unit tests for VTOP_OR_CRASH (0x00E6D1E8)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "os/os_internal.h"

static jmp_buf crash_jmp;
static int n_crash;
static status_$t crash_status, vtop_status;
static uint32_t vtop_va;

void CRASH_SYSTEM(const status_$t *s) { n_crash++; crash_status = *s; longjmp(crash_jmp, 1); }
uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    vtop_va = va; *status = vtop_status; return 0x345;
}

#include "../vtop_or_crash.c"

static void reset_state(void) { n_crash = 0; vtop_status = 0; vtop_va = 0; }

static void test_translates(void)
{
    uint32_t va = 0x00FC0400;
    ASSERT_EQ(0x345, VTOP_OR_CRASH(&va));
    ASSERT_EQ(0x00FC0400, vtop_va);
    ASSERT_EQ(0, n_crash);
}

static void test_failure_crashes(void)
{
    uint32_t va = 0x1000;
    vtop_status = 0x00040004;
    if (setjmp(crash_jmp) == 0) {
        VTOP_OR_CRASH(&va);
    }
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x00040004, crash_status);
}

int main(void)
{
    printf("VTOP_OR_CRASH tests\n");
    RUN_TEST(translates);
    RUN_TEST(failure_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
