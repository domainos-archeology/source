/*
 * acl/test/test_get_sid.c - ACL_$GET_SID (0x00E74C24): the current
 * process's ORIGINAL SID block (two-argument frame).
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

MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);
uint16_t PROC1_$CURRENT;

#include "../get_sid.c"

TEST(original_block)
{
    acl_sid_block_t b;
    status_$t st = 7;
    memset(&ACL_$DATA, 0, sizeof ACL_$DATA);
    PROC1_$CURRENT = 64;
    ACL_$DATA.original_sids[64].org_sid.low = 0x77;
    ACL_$DATA.current_sids[64].org_sid.low = 0x88;
    ACL_$GET_SID(&b, &st);
    ASSERT_EQ(0x77, b.org_sid.low);
    ASSERT_EQ(0, st);
}

int main(void)
{
    printf("ACL_$GET_SID tests:\n");
    RUN_TEST(original_block);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
