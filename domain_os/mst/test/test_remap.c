/*
 * mst/test/test_remap.c - MST_$REMAP (0x00E439D4): MST_$REMAP_PRIVI with
 * the zero flags word, its result passed back.
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

static uint16_t r_flags;
static uint32_t *r_args[5];
static status_$t *r_status;
static uint32_t result_cell;

void *MST_$REMAP_PRIVI(uint16_t *flags, uint32_t *va_ptr, uint32_t *unmap_len,
                       uint32_t *offset_ptr, uint32_t *length_ptr,
                       uint32_t *map_info, status_$t *status)
{
    r_flags = *flags;
    r_args[0] = va_ptr; r_args[1] = unmap_len; r_args[2] = offset_ptr;
    r_args[3] = length_ptr; r_args[4] = map_info; r_status = status;
    return &result_cell;
}

#include "../remap.c"

TEST(forwards)
{
    uint32_t a = 1, b = 2, c = 3, d = 4, e = 5;
    status_$t st;
    ASSERT_EQ(1, MST_$REMAP(&a, &b, &c, &d, &e, &st) == &result_cell);
    ASSERT_EQ(0, r_flags);
    ASSERT_EQ(1, r_args[0] == &a && r_args[1] == &b && r_args[2] == &c);
    ASSERT_EQ(1, r_args[3] == &d && r_args[4] == &e && r_status == &st);
}

int main(void)
{
    printf("MST_$REMAP tests:\n");
    RUN_TEST(forwards);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
