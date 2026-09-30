/*
 * misc/test/test_print_build_time.c - PRINT_BUILD_TIME (0x00E38000): the
 * banner from GET_BUILD_TIME printed with "%/%a%/%.".
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

#include <stdarg.h>
#include "misc/misc_internal.h"

static char *g_buf;
static int16_t *g_len;
static const char *w_fmt;
static void *w_a1, *w_a2;
static int n_write;

void GET_BUILD_TIME(char *buf, int16_t *len_p)
{
    g_buf = buf; g_len = len_p;
    memcpy(buf, "Domain/OS", 9);
    *len_p = 9;
}

void VFMT_$WRITE10(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    w_fmt = format;
    w_a1 = va_arg(ap, void *);
    w_a2 = va_arg(ap, void *);
    va_end(ap);
    n_write++;
}

#include "../print_build_time.c"

TEST(prints_banner)
{
    PRINT_BUILD_TIME();
    ASSERT_EQ(1, n_write);
    ASSERT_EQ(0, memcmp(w_fmt, "%/%a%/%.", 8));
    ASSERT_EQ(1, w_a1 == (void *)g_buf);
    ASSERT_EQ(1, w_a2 == (void *)g_len);
}

int main(void)
{
    printf("PRINT_BUILD_TIME tests:\n");
    RUN_TEST(prints_banner);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
