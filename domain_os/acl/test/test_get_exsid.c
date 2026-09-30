/*
 * acl/test/test_get_exsid.c - ACL_$GET_EXSID (0x00E48972): the SID block,
 * ACL_$GET_PROJ_LIST's arguments, and all eight project slots copied.
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

static int16_t seen_max;
static status_$t *seen_status;

void ACL_$GET_PROJ_LIST(uid_t *proj_acls, int16_t *max_count, int16_t *count_ret,
                        status_$t *status_ret)
{
    seen_max = *max_count;
    seen_status = status_ret;
    for (int i = 0; i < 8; i++) { proj_acls[i].high = 0x100 + i; proj_acls[i].low = i; }
    *count_ret = 2;
    *status_ret = 0x55;
}

#include "../get_exsid.c"

TEST(record)
{
    acl_$exsid_t x;
    status_$t st = 0;
    memset(&ACL_$DATA, 0, sizeof ACL_$DATA);
    PROC1_$CURRENT = 2;
    ACL_$DATA.current_sids[2].group_sid.high = 0x42;
    ACL_$GET_EXSID(&x, &st);
    ASSERT_EQ(0x42, x.sids.group_sid.high);
    ASSERT_EQ(8, seen_max);
    ASSERT_EQ(1, seen_status == &st);
    ASSERT_EQ(0x55, st);
    ASSERT_EQ(0x100, x.proj_uids[0].high);
    ASSERT_EQ(0x107, x.proj_uids[7].high);
    ASSERT_EQ(7, x.proj_uids[7].low);
}

int main(void)
{
    printf("ACL_$GET_EXSID tests:\n");
    RUN_TEST(record);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
