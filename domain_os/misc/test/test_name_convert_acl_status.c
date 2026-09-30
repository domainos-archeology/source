/*
 * misc/test/test_name_convert_acl_status.c - NAME_CONVERT_ACL_STATUS
 * (0x00E5861C): the three rewrites and the pass-through.
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

#include "../name_convert_acl_status.c"

static status_$t conv(status_$t s) { NAME_CONVERT_ACL_STATUS(&s); return s; }

TEST(mappings)
{
    ASSERT_EQ(0x000E0013, conv(0x00230001));
    ASSERT_EQ(0x000E0014, conv(0x00230002));
    ASSERT_EQ(0x000E000E, conv(0x00230004));
}

TEST(others_unchanged)
{
    ASSERT_EQ(0x00230003, conv(0x00230003));
    ASSERT_EQ(0, conv(0));
    ASSERT_EQ(0x000E0013, conv(0x000E0013));
}

int main(void)
{
    printf("NAME_CONVERT_ACL_STATUS tests:\n");
    RUN_TEST(mappings);
    RUN_TEST(others_unchanged);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
