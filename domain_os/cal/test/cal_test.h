/*
 * cal/test/cal_test.h - Shared scaffolding for the CAL unit tests
 *
 * Each cal/test/test_*.c is a self-contained host program that #includes
 * the cal/*.c file(s) under test directly.  This header supplies the tiny
 * TEST/RUN_TEST/ASSERT framework used across the subsystem's tests.
 *
 * The kernel headers must be included before any host header so that the
 * Domain/OS definitions of clock_t, uid_t, true/false, etc. win.
 */

#ifndef CAL_TEST_H
#define CAL_TEST_H

#include "cal/cal_internal.h"

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while (0)

/*
 * ASSERT_EQ(a, b) - both sides are widened to unsigned long and compared.
 * The CAL tests were written as ASSERT_EQ(actual, expected); the message
 * therefore reports both operands rather than labelling one "expected".
 */
#define ASSERT_EQ(a, b) do { \
    unsigned long _a = (unsigned long)(a); \
    unsigned long _b = (unsigned long)(b); \
    if (_a != _b) { \
        printf("FAILED\n    %s = 0x%lx (%lu), %s = 0x%lx (%lu) at line %d\n", \
               #a, _a, _a, #b, _b, _b, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s is false at line %d\n", #cond, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* Kept for tests written against the m68k byte layout; the cal code is now
 * host-endian safe, so this is simply RUN_TEST. */
#define RUN_TEST_BE_ONLY(name) RUN_TEST(name)

#define TEST_SUMMARY() do { \
    printf("\nResults: %d passed, %d failed, %d skipped\n", \
           tests_passed, tests_failed, tests_skipped); \
} while (0)

#endif /* CAL_TEST_H */
