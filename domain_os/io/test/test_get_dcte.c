/*
 * io/test/test_get_dcte.c - IO_$GET_DCTE (0x00E1A448): the list walk, the
 * cstatus copy, the keep-walking-on-a-bad-status quirk and the not-found
 * status.
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

dcte_t *IO_$DCTE_LIST;

#include "../get_dcte.c"

static dcte_t d[4];

static void setup(void)
{
    memset(d, 0, sizeof(d));
    d[0].ctype = 0; d[0].cnum = 0; d[0].nextp = &d[1];
    d[1].ctype = 1; d[1].cnum = 0; d[1].nextp = &d[2];
    d[2].ctype = 1; d[2].cnum = 1; d[2].nextp = &d[3];
    d[3].ctype = 1; d[3].cnum = 0; d[3].nextp = NULL;
    IO_$DCTE_LIST = &d[0];
}

TEST(finds_first_good_match)
{
    uint16_t t = 1, n = 1;
    status_$t st = 0x55;
    setup();
    ASSERT_EQ((uintptr_t)&d[2], (uintptr_t)IO_$GET_DCTE(&t, &n, &st));
    ASSERT_EQ(0, st);
}

TEST(bad_cstatus_keeps_walking_to_a_later_match)
{
    uint16_t t = 1, n = 0;
    status_$t st = 0;
    setup();
    d[1].cstatus = 0x00100002;
    ASSERT_EQ((uintptr_t)&d[3], (uintptr_t)IO_$GET_DCTE(&t, &n, &st));
    ASSERT_EQ(0, st);
    d[3].cstatus = 0x00100003;
    ASSERT_EQ((uintptr_t)&d[3], (uintptr_t)IO_$GET_DCTE(&t, &n, &st));
    ASSERT_EQ(0x00100003, st);
}

TEST(not_found)
{
    uint16_t t = 2, n = 0;
    status_$t st = 0;
    setup();
    ASSERT_EQ(0, (uintptr_t)IO_$GET_DCTE(&t, &n, &st));
    ASSERT_EQ(status_$io_controller_not_found, st);
    IO_$DCTE_LIST = NULL;
    t = 0;
    ASSERT_EQ(0, (uintptr_t)IO_$GET_DCTE(&t, &n, &st));
    ASSERT_EQ(status_$io_controller_not_found, st);
}

int main(void)
{
    printf("IO_$GET_DCTE\n");
    RUN_TEST(finds_first_good_match);
    RUN_TEST(bad_cstatus_keeps_walking_to_a_later_match);
    RUN_TEST(not_found);
    printf("%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed ? 1 : 0;
}
