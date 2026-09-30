/*
 * mst/test/test_unmaps.c - MST_$UNMAPS: MST_$UNMAP_PRIVI with mode 0x0,
 * UID_$NIL, the dereferenced range and PROC1_$AS_ID.
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

uint16_t PROC1_$AS_ID;
uid_t UID_$NIL = { 0, 0 };

static int n_calls;
static int16_t s_mode;
static uid_t *s_uid;
static uint32_t s_start, s_size;
static uint16_t s_asid;
static status_$t *s_status;

void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status_ret)
{
    n_calls++;
    s_mode = mode; s_uid = uid; s_start = start; s_size = size;
    s_asid = asid; s_status = status_ret;
    *status_ret = 0;
}

#include "../unmaps.c"

TEST(forwards)
{
    uid_t u = { 1, 2 };
    uint32_t start = 0x00123000u, len = 0x8000u;
    status_$t st = 5;
    PROC1_$AS_ID = 7;
    n_calls = 0;
    MST_$UNMAPS(&start, &len, &st);
    ASSERT_EQ(1, n_calls);
    ASSERT_EQ(0x0, s_mode);
    ASSERT_EQ(1, s_uid == &UID_$NIL);
    ASSERT_EQ(0x00123000u, s_start);
    ASSERT_EQ(0x8000u, s_size);
    ASSERT_EQ(7, s_asid);
    ASSERT_EQ(1, s_status == &st);
    ASSERT_EQ(0, st);
    (void)u;
}

int main(void)
{
    printf("MST_$UNMAPS tests:\n");
    RUN_TEST(forwards);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
