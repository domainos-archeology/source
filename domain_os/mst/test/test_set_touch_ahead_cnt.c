/*
 * mst/test/test_set_touch_ahead_cnt.c - MST_$SET_TOUCH_AHEAD_CNT
 * (0x00E446FC): MST_$PRIV_SET_TOUCH_AHEAD_CNT with the zero flags word.
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

#include "mst/mst_internal.h"

static uint16_t p_flags;
static void *p_args[5];

void MST_$PRIV_SET_TOUCH_AHEAD_CNT(uint16_t *flags, uint32_t *va_ptr,
                                   uint32_t *length_ptr, int16_t *count_ptr,
                                   void *result_ptr, status_$t *status)
{
    p_flags = *flags;
    p_args[0] = va_ptr; p_args[1] = length_ptr; p_args[2] = count_ptr;
    p_args[3] = result_ptr; p_args[4] = status;
}

#include "../set_touch_ahead_cnt.c"

TEST(forwards)
{
    uint32_t va = 0, len = 0, res = 0;
    int16_t cnt = 8;
    status_$t st;
    p_flags = 0xFFFF;
    MST_$SET_TOUCH_AHEAD_CNT(&va, &len, &cnt, &res, &st);
    ASSERT_EQ(0, p_flags);
    ASSERT_EQ(1, p_args[0] == &va && p_args[1] == &len && p_args[2] == &cnt);
    ASSERT_EQ(1, p_args[3] == &res && p_args[4] == &st);
}

int main(void)
{
    printf("MST_$SET_TOUCH_AHEAD_CNT tests:\n");
    RUN_TEST(forwards);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
