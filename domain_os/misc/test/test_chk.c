/*
 * misc/test/test_chk.c - CHK (0x00E1A404): true only when a DCTE of the
 * type has a zero cstatus; a non-zero match does not stop the walk.
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
#include "io/io.h"

dcte_t *IO_$DCTE_LIST;

#include "../chk.c"

static dcte_t d[3];

static void chain(void)
{
    memset(d, 0, sizeof d);
    d[0].ctype = 0; d[0].nextp = &d[1];
    d[1].ctype = 1; d[1].nextp = &d[2]; d[1].cstatus = 0x00080001;
    d[2].ctype = 1; d[2].nextp = NULL;
    IO_$DCTE_LIST = &d[0];
}

TEST(empty_list)
{
    uint16_t t = 0;
    IO_$DCTE_LIST = NULL;
    ASSERT_EQ(0, CHK(&t));
}

TEST(match_ok)
{
    uint16_t t = 0;
    chain();
    ASSERT_EQ(0xFF, (uint8_t)CHK(&t));
}

TEST(later_match_ok)
{
    uint16_t t = 1;
    chain();
    ASSERT_EQ(0xFF, (uint8_t)CHK(&t));
    d[2].cstatus = 5;
    ASSERT_EQ(0, CHK(&t));
}

TEST(no_match)
{
    uint16_t t = 2;
    chain();
    ASSERT_EQ(0, CHK(&t));
}

int main(void)
{
    printf("CHK tests:\n");
    RUN_TEST(empty_list);
    RUN_TEST(match_ok);
    RUN_TEST(later_match_ok);
    RUN_TEST(no_match);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
