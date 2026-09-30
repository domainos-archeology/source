/*
 * acl/test/test_get_pid_sid.c - ACL_$GET_PID_SID (0x00E489E2): a given
 * process's CURRENT SID block, all nine longwords.
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

#include "../get_pid_sid.c"

TEST(current_block)
{
    uid_t out[5];
    status_$t st = 7;
    memset(&ACL_$DATA, 0, sizeof ACL_$DATA);
    PROC1_$CURRENT = 1;
    ACL_$DATA.current_sids[3].user_sid.high = 0x31;
    ACL_$DATA.current_sids[3].pad = 0x39;
    ACL_$DATA.original_sids[3].user_sid.high = 0x55;
    ACL_$GET_PID_SID(3, out, &st);
    ASSERT_EQ(0x31, out[0].high);
    ASSERT_EQ(0x39, ((acl_sid_block_t *)out)->pad);
    ASSERT_EQ(0, st);
}

int main(void)
{
    printf("ACL_$GET_PID_SID tests:\n");
    RUN_TEST(current_block);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
