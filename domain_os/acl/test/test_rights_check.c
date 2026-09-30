/*
 * acl/test/test_rights_check.c - unit tests for ACL_$RIGHTS_CHECK (0x00E46AEC).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "acl/acl_internal.h"

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
uint16_t PROC1_$CURRENT;

static acl_sid_block_t *e_sids;
static uid_t *e_proj, e_uid;
static boolean e_ignore, e_super, e_subsys;
static uint32_t e_mask;
static int16_t e_opts;
static status_$t *e_status;

uint32_t acl_$eval_rights(acl_sid_block_t *sids, uid_t *proj_uids, uid_t *uid,
                          boolean ignore_super, uint32_t required_mask,
                          int16_t option_flags, boolean in_super,
                          boolean in_subsys, status_$t *status_ret)
{
    e_sids = sids; e_proj = proj_uids; e_uid = *uid; e_ignore = ignore_super;
    e_mask = required_mask; e_opts = option_flags; e_super = in_super;
    e_subsys = in_subsys; e_status = status_ret;
    *status_ret = 0;
    return 0x12345678u;
}

#include "../rights_check.c"

static struct {
    acl_sid_block_t sids;
    uid_t proj[8];
} ctx;

TEST(forwards)
{
    uid_t f = { 0x11, 0x22 };
    uint32_t mask = 0x1F;
    int16_t opts = 0x0100;
    int8_t chk = -1;
    status_$t st;
    uint32_t r;

    PROC1_$CURRENT = 4;
    ACL_$UNWIRED_DATA.super_count[4] = 2;
    r = ACL_$RIGHTS_CHECK(&ctx, &f, &mask, &opts, &chk, &st);
    ASSERT_EQ(0x12345678u, r);
    ASSERT_EQ((uintptr_t)&ctx.sids, (uintptr_t)e_sids);
    ASSERT_EQ((uintptr_t)&ctx.proj[0], (uintptr_t)e_proj);
    ASSERT_EQ(0x22, e_uid.low);
    ASSERT_EQ(0, (uint8_t)e_ignore);
    ASSERT_EQ(0x1F, e_mask);
    ASSERT_EQ(0x0100, (uint16_t)e_opts);
    ASSERT_EQ(0xFF, (uint8_t)e_super);
    ASSERT_EQ(0xFF, (uint8_t)e_subsys);
    ASSERT_EQ((uintptr_t)&st, (uintptr_t)e_status);

    chk = 0;
    ACL_$UNWIRED_DATA.super_count[4] = 0;
    ACL_$RIGHTS_CHECK(&ctx, &f, &mask, &opts, &chk, &st);
    ASSERT_EQ(0, (uint8_t)e_super);
    ASSERT_EQ(0, (uint8_t)e_subsys);

    ACL_$UNWIRED_DATA.super_count[4] = -1;      /* sgt: signed */
    ACL_$RIGHTS_CHECK(&ctx, &f, &mask, &opts, &chk, &st);
    ASSERT_EQ(0, (uint8_t)e_super);
}

int main(void)
{
    printf("ACL_$RIGHTS_CHECK tests\n");
    RUN_TEST(forwards);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
