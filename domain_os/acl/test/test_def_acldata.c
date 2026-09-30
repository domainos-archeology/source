/*
 * acl/test/test_def_acldata.c - ACL_$DEF_ACLDATA (0x00E478DC): the default
 * protection record and UID_$NIL.
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
uid_t PPO_$NIL_USER_UID = { 0x00800001u, 0 };
uid_t RGYC_$G_NIL_UID   = { 0x00800040u, 0 };
uid_t PPO_$NIL_ORG_UID  = { 0x00800080u, 0 };

#include "../def_acldata.c"

TEST(record)
{
    acl_$prot_data_t p;
    uid_t u = { 1, 2 };
    memset(&p, 0xAA, sizeof p);
    ACL_$DEF_ACLDATA(&p, &u);
    ASSERT_EQ(0x00800001u, p.owner.high);
    ASSERT_EQ(0x00800040u, p.group.high);
    ASSERT_EQ(0x00800080u, p.org.high);
    ASSERT_EQ(0, p.org.low);
    ASSERT_EQ(0x10, p.owner_rights);
    ASSERT_EQ(0x10, p.group_rights);
    ASSERT_EQ(0x10, p.org_rights);
    ASSERT_EQ(0x0F, p.world_rights);
    ASSERT_EQ(0, p.subsys_rights);
    ASSERT_EQ(0, p.reserved_1d[0]);
    ASSERT_EQ(0, p.reserved_1d[2]);
    ASSERT_EQ(0x0C, p.owner_ext[0]);
    ASSERT_EQ(0x0C, p.owner_ext[1]);
    ASSERT_EQ(0x0C, p.owner_ext[2]);
    ASSERT_EQ(0, u.high);
    ASSERT_EQ(0, u.low);
}

int main(void)
{
    printf("ACL_$DEF_ACLDATA tests:\n");
    RUN_TEST(record);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
