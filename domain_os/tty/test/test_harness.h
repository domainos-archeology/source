/*
 * tty/test/test_harness.h - shared TEST/RUN_TEST/ASSERT helpers and a
 * printf-style call log for the tty unit tests.  Each test program is still
 * self-contained: this header has no code that needs linking.
 */
#ifndef TTY_TEST_HARNESS_H
#define TTY_TEST_HARNESS_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

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
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_STR(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAILED\n    Expected: \"%s\", Got: \"%s\" at line %d\n", \
               (expected), (actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

static char call_log[1024];

static void logf_call(const char *fmt, ...)
{
    va_list ap;
    size_t used = strlen(call_log);
    va_start(ap, fmt);
    vsnprintf(call_log + used, sizeof(call_log) - used, fmt, ap);
    va_end(ap);
}

static void log_reset(void) { call_log[0] = '\0'; }

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed); \
    return tests_failed ? 1 : 0; \
} while (0)

#endif
