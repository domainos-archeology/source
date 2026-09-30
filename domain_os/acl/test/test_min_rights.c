/*
 * acl/test/test_min_rights.c - unit tests for ACL_$MIN_RIGHTS (0x00E468E2).
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

MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);
uid_t UID_$NIL = { 0, 0 };

static status_$t attr_st, find_st;
static uint8_t o_r, g_r, org_r, w_r;
static uid_t def_acl;
static int16_t find_ret;
static int n_lock, n_unlock, n_find;
static acl_$prot_data_t *find_prot;

void acl_$get_obj_acl_attrs(uid_t *uid, file_$obj_loc_t *loc,
                            ast_$acl_attr_t *attrs, status_$t *status_ret)
{
    acl_$prot_data_t *p = ACL_$PROT_DATA(attrs);
    (void)uid; (void)loc;
    p->owner_rights = o_r; p->group_rights = g_r; p->org_rights = org_r;
    p->world_rights = w_r;
    attrs->default_acl = def_acl;
    *status_ret = attr_st;
}
int16_t acl_$find_acl_slot(uid_t *acl_uid, int8_t *cached_flag_ret,
                           acl_$prot_data_t *prot, status_$t *status_ret)
{
    (void)acl_uid; (void)cached_flag_ret;
    n_find++; find_prot = prot;
    *status_ret = find_st;
    return find_ret;
}
void ML_$LOCK(int16_t id) { if (id == ML_LOCK_ACL) n_lock++; }
void ML_$UNLOCK(int16_t id) { if (id == ML_LOCK_ACL) n_unlock++; }

#include "../min_rights.c"

static uid_t obj = { 1, 2 };

static void reset(void)
{
    attr_st = find_st = 0;
    o_r = g_r = org_r = 0; w_r = 0x0F;
    def_acl = UID_$NIL;
    find_ret = -1;
    n_lock = n_unlock = n_find = 0;
}

TEST(attr_error)
{
    reset(); attr_st = 0x00210001;
    ASSERT_EQ(0, ACL_$MIN_RIGHTS(&obj));
}

TEST(world_less_owner_group_org)
{
    reset();
    o_r = 0x01; g_r = 0x12; org_r = 0x04;   /* group has bit 4: kept */
    ASSERT_EQ(0x0A, ACL_$MIN_RIGHTS(&obj));
    ASSERT_EQ(0, n_lock);

    reset(); w_r = 0xFF;
    ASSERT_EQ(0x0F, ACL_$MIN_RIGHTS(&obj));
}

TEST(default_acl_entries)
{
    acl_$cache_slot_t *slot = &ACL_$DATA.acl_cache[3];

    reset();
    def_acl.high = 0x55;
    find_ret = 3;
    slot->entry_count = 2;
    ACL_$CACHE_ENTRY(slot, 1)->rights = 0x0001;
    ACL_$CACHE_ENTRY(slot, 2)->rights = 0x0004;
    ACL_$CACHE_ENTRY(slot, 3)->rights = 0x0008;   /* past entry_count */
    ASSERT_EQ(0x0A, ACL_$MIN_RIGHTS(&obj));
    ASSERT_EQ(1, n_lock);
    ASSERT_EQ(1, n_unlock);

    slot->entry_count = 0;
    ASSERT_EQ(0x0F, ACL_$MIN_RIGHTS(&obj));

    reset(); def_acl.high = 0x55; find_ret = -1;
    ASSERT_EQ(0x0F, ACL_$MIN_RIGHTS(&obj));
    ASSERT_EQ(1, n_unlock);

    reset(); def_acl.high = 0x55; find_st = 0x00210002;
    ASSERT_EQ(0, ACL_$MIN_RIGHTS(&obj));
    ASSERT_EQ(1, n_unlock);
}

int main(void)
{
    printf("ACL_$MIN_RIGHTS tests\n");
    RUN_TEST(attr_error);
    RUN_TEST(world_less_owner_group_org);
    RUN_TEST(default_acl_entries);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
