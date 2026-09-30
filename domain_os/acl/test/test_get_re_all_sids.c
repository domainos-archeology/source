/*
 * acl/test/test_get_re_all_sids.c - ACL_$GET_RE_ALL_SIDS (0x00E48792):
 * the four per-process copies and the cleared status.
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

#include "../get_re_all_sids.c"

TEST(copies)
{
    acl_sid_block_t o, c;
    acl_proj_list_t sp, pl;
    status_$t st = 0x1234;
    memset(&ACL_$DATA, 0, sizeof ACL_$DATA);
    PROC1_$CURRENT = 5;
    ACL_$DATA.original_sids[5].user_sid.high = 0x0A;
    ACL_$DATA.original_sids[5].pad = 0x0B;
    ACL_$DATA.current_sids[5].login_sid.low = 0x0C;
    ACL_$DATA.saved_proj[5].field_08 = 0x0D;
    ACL_$DATA.proj_lists[5].field_04 = 0x0E;
    ACL_$DATA.original_sids[4].user_sid.high = 0x99;
    ACL_$GET_RE_ALL_SIDS(&o, &c, &sp, &pl, &st);
    ASSERT_EQ(0x0A, o.user_sid.high);
    ASSERT_EQ(0x0B, o.pad);
    ASSERT_EQ(0x0C, c.login_sid.low);
    ASSERT_EQ(0x0D, sp.field_08);
    ASSERT_EQ(0x0E, pl.field_04);
    ASSERT_EQ(0, st);
}

int main(void)
{
    printf("ACL_$GET_RE_ALL_SIDS tests:\n");
    RUN_TEST(copies);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
