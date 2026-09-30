/*
 * io/test/test_get_config.c - IO_$GET_CONFIG (0x00E72334): the CHK probes
 * in the image's order (types 0, 1, 2) and the +0x550 flag.
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

#include "io/io_internal.h"
#include "misc/misc.h"

int8_t io_$config_flag;
static int8_t chk_answer[3];
static uint16_t chk_order[4];
static int n_chk;

int8_t CHK(const uint16_t *ctype)
{
    chk_order[n_chk++] = *ctype;
    return chk_answer[*ctype];
}

#include "../get_config.c"

TEST(none)
{
    uint16_t a = 0xFFFF, b = 0xFFFF, c = 0xFFFF, d = 0xFFFF;
    memset(chk_answer, 0, 3); io_$config_flag = 0; n_chk = 0;
    IO_$GET_CONFIG(&a, &b, &c, &d);
    ASSERT_EQ(0, a); ASSERT_EQ(0, b); ASSERT_EQ(0, c); ASSERT_EQ(0, d);
    ASSERT_EQ(3, n_chk);
    ASSERT_EQ(0, chk_order[0]); ASSERT_EQ(1, chk_order[1]); ASSERT_EQ(2, chk_order[2]);
}

TEST(all)
{
    uint16_t a, b, c = 0x55, d;
    memset(chk_answer, 0xFF, 3); io_$config_flag = (int8_t)0xFF; n_chk = 0;
    IO_$GET_CONFIG(&a, &b, &c, &d);
    ASSERT_EQ(3, a); ASSERT_EQ(1, b); ASSERT_EQ(0, c); ASSERT_EQ(1, d);
}

TEST(positive_is_false)
{
    uint16_t a, b, c, d;
    chk_answer[0] = 1; chk_answer[1] = 0; chk_answer[2] = 0x7F;
    io_$config_flag = 1; n_chk = 0;
    IO_$GET_CONFIG(&a, &b, &c, &d);
    ASSERT_EQ(0, a); ASSERT_EQ(0, b); ASSERT_EQ(0, d);
}

int main(void)
{
    printf("IO_$GET_CONFIG tests:\n");
    RUN_TEST(none);
    RUN_TEST(all);
    RUN_TEST(positive_is_false);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
