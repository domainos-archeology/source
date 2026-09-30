/*
 * acl/test/test_default_acl.c - ACL_$DEFAULT_ACL (0x00E4787E): the type
 * to default-ACL mapping and the untouched output for other types.
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

#include "acl/acl_internal.h"

uid_t UID_$NIL = { 0, 0 };
uid_t ACL_$FNDWRX = { 0x11111111u, 0x22222222u };
uid_t ACL_$DNDCAL = { 0x33333333u, 0x44444444u };

#include "../default_acl.c"

static uid_t run(int16_t type)
{
    uid_t out = { 0xDEADBEEFu, 0xCAFEF00Du };
    ACL_$DEFAULT_ACL(&out, &type);
    return out;
}

TEST(files)
{
    int16_t t[3] = { 0, 4, 5 };
    for (int i = 0; i < 3; i++) {
        uid_t u = run(t[i]);
        ASSERT_EQ(0x11111111u, u.high);
        ASSERT_EQ(0x22222222u, u.low);
    }
}

TEST(dirs)
{
    uid_t u = run(1);
    ASSERT_EQ(0x33333333u, u.high);
    u = run(2);
    ASSERT_EQ(0x44444444u, u.low);
}

TEST(nil)
{
    uid_t u = run(3);
    ASSERT_EQ(0, u.high);
    ASSERT_EQ(0, u.low);
}

TEST(other_unchanged)
{
    uid_t u = run(6);
    ASSERT_EQ(0xDEADBEEFu, u.high);
    u = run(-1);
    ASSERT_EQ(0xCAFEF00Du, u.low);
}

int main(void)
{
    printf("ACL_$DEFAULT_ACL tests:\n");
    RUN_TEST(files);
    RUN_TEST(dirs);
    RUN_TEST(nil);
    RUN_TEST(other_unchanged);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
