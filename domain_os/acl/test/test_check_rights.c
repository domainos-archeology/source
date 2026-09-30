/*
 * acl/test/test_check_rights.c - ACL_$CHECK_RIGHTS (0x00E46A8E): the
 * arguments handed to acl_$eval_rights and its result passed back.
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

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);

static acl_sid_block_t *s_sids;
static uid_t *s_proj, s_uid;
static int s_ign, s_in_super, s_in_subsys;
static uint32_t s_mask;
static int16_t s_opt;

uint32_t acl_$eval_rights(acl_sid_block_t *sids, uid_t *proj_uids, uid_t *uid,
                          boolean ignore_super, uint32_t required_mask,
                          int16_t option_flags, boolean in_super,
                          boolean in_subsys, status_$t *status_ret)
{
    s_sids = sids; s_proj = proj_uids; s_uid = *uid;
    s_ign = ignore_super; s_mask = required_mask; s_opt = option_flags;
    s_in_super = in_super; s_in_subsys = in_subsys;
    *status_ret = 0;
    return 0x1F;
}

#include "../check_rights.c"

TEST(forwarding)
{
    acl_$exsid_t ctx;
    uid_t f = { 0xABCD, 0x1234 };
    uint32_t mask = 0x12;
    int16_t opt = 3;
    status_$t st = 9;
    PROC1_$CURRENT = 4;
    ACL_$UNWIRED_DATA.super_count[4] = 1;
    ASSERT_EQ(0x1F, ACL_$CHECK_RIGHTS(&ctx, &f, &mask, &opt, &st));
    ASSERT_EQ(1, s_sids == &ctx.sids);
    ASSERT_EQ(1, s_proj == &ctx.proj_uids[0]);
    ASSERT_EQ(0xABCD, s_uid.high);
    ASSERT_EQ(0, s_ign);
    ASSERT_EQ(0x12, s_mask);
    ASSERT_EQ(3, s_opt);
    ASSERT_EQ(1, s_in_super != 0);
    ASSERT_EQ(0, s_in_subsys);
    ACL_$UNWIRED_DATA.super_count[4] = 0;
    ACL_$CHECK_RIGHTS(&ctx, &f, &mask, &opt, &st);
    ASSERT_EQ(0, s_in_super);
}

int main(void)
{
    printf("ACL_$CHECK_RIGHTS tests:\n");
    RUN_TEST(forwarding);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
