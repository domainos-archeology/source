/*
 * misc/test/test_get_wired.c - GET_WIRED (0x00E1D8DC): the address of the
 * AUDIT wired segment.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
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

#include "misc/misc_internal.h"
#include "audit/audit.h"

MODULE_DATA_DEFINE(audit_$wired_ec_t, audit_$wired_ec, 0x00E2E07C);

#include "../get_wired.c"

TEST(address)
{
    ASSERT_EQ(1, GET_WIRED() == (void *)&audit_$wired_ec);
    ASSERT_EQ(0x00E2E07C, MODULE_DATA_ADDR(audit_$wired_ec));
}

int main(void)
{
    printf("GET_WIRED tests:\n");
    RUN_TEST(address);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
