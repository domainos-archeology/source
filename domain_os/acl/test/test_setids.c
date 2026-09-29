/*
 * acl/test/test_setids.c - unit tests for acl_$setids (0x00E46B4E)
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "acl/acl_internal.h"
#include "rem_file/rem_file.h"

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);
uid_t UID_$NIL = { 0, 0 };

/* ---- mocks ----------------------------------------------------------- */

static ast_$acl_attr_t mock_attrs;
static int8_t mock_remote;
static status_$t mock_attr_status;
static int16_t mock_slot;
static status_$t mock_slot_status;
static int slot_calls, lock_calls, unlock_calls, rem_calls;
static int8_t mock_rem_changed;
static status_$t mock_rem_status;

void acl_$get_obj_acl_attrs(uid_t *uid, file_$obj_loc_t *loc,
                            ast_$acl_attr_t *attrs, status_$t *status_ret)
{
    (void)uid;
    memset(loc, 0, sizeof(*loc));
    loc->flags = mock_remote;
    *attrs = mock_attrs;
    *status_ret = mock_attr_status;
}

int16_t acl_$find_acl_slot(uid_t *acl_uid, int8_t *cached_flag_ret,
                           acl_$prot_data_t *prot, status_$t *status_ret)
{
    (void)acl_uid; (void)prot;
    slot_calls++;
    *cached_flag_ret = 0;
    *status_ret = mock_slot_status;
    return mock_slot;
}

void ML_$LOCK(int16_t id) { (void)id; lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlock_calls++; }

void REM_FILE_$ACL_SETIDS(void *addr_info, uid_t *acl_uid, void *sid_data,
                          void *owner_data, int8_t *modified_flag_out,
                          status_$t *status)
{
    (void)addr_info; (void)acl_uid; (void)sid_data; (void)owner_data;
    rem_calls++;
    *modified_flag_out = mock_rem_changed;
    *status = mock_rem_status;
}

#include "../setids.c"

/* ---- helpers --------------------------------------------------------- */

static uid_t obj = { 0x11, 0x22 };
static uid_t sids[4];
static uint32_t ext[3];
static int8_t changed;

static acl_$prot_data_t *prot(void) { return ACL_$PROT_DATA(&mock_attrs); }

static void reset_state(void)
{
    memset(&mock_attrs, 0, sizeof(mock_attrs));
    memset(&ACL_$DATA, 0, sizeof(ACL_$DATA));
    memset(sids, 0, sizeof(sids));
    memset(ext, 0, sizeof(ext));
    ACL_$UNWIRED_DATA.local_locksmith = 0;
    mock_attrs.obj_flags[ACL_ATTR_FLAGS_LO] = ACL_ATTR_FLAG_LOCAL;
    mock_attrs.default_acl.high = 0x500;
    mock_remote = 0;
    mock_attr_status = status_$ok;
    mock_slot = 2;
    mock_slot_status = status_$ok;
    slot_calls = lock_calls = unlock_calls = rem_calls = 0;
    mock_rem_changed = 0;
    mock_rem_status = status_$ok;
    changed = 0x55;
}

static status_$t run(int8_t set)
{
    status_$t st = 0x99;
    acl_$setids(&obj, set, sids, ext, &changed, &st);
    return st;
}

/* ---- tests ----------------------------------------------------------- */

static void test_attr_failure_sets_bit31(void)
{
    mock_attr_status = 0x000F0001;
    ASSERT_EQ(0x800F0001u, (uint32_t)run((int8_t)0xFF));
    ASSERT_EQ(0, lock_calls);
}

static void test_set_copies_setid_sids(void)
{
    prot()->owner.high = 0x100;
    prot()->owner_rights = 0x20;
    prot()->owner_ext[0] = 0xAA;
    prot()->group.high = 0x200;                 /* no set-ID bit */
    prot()->org.high = 0x300;
    prot()->org_rights = 0x27;
    prot()->owner_ext[2] = 0xCC;
    ACL_$DATA.acl_cache[2].required_uid.high = 0x400;

    ASSERT_EQ(status_$ok, run((int8_t)0xFF));
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)changed);
    ASSERT_EQ(0x100, sids[0].high);
    ASSERT_EQ(0xAA, ext[0]);
    ASSERT_EQ(0, sids[1].high);
    ASSERT_EQ(0x300, sids[2].high);
    ASSERT_EQ(0xCC, ext[2]);
    ASSERT_EQ(0x400, sids[3].high);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
}

static void test_check_mismatch(void)
{
    prot()->group.high = 0x200;
    prot()->group_rights = 0x20;
    sids[1].high = 0x201;
    ASSERT_EQ(status_$acl_no_right_to_set_subsystem_data, run(0));
    ASSERT_EQ(0, changed);
    ASSERT_EQ(1, unlock_calls);

    sids[1].high = 0x200;                       /* now it matches */
    ACL_$DATA.acl_cache[2].required_uid.high = 0x400;
    sids[3].high = 0x401;
    ASSERT_EQ(status_$acl_no_right_to_set_subsystem_data, run(0));
    sids[3].high = 0x400;
    ASSERT_EQ(status_$ok, run(0));
}

static void test_nil_required_uid_clears_slot3(void)
{
    sids[3].high = 0x999;
    ASSERT_EQ(status_$ok, run(0));
    ASSERT_EQ(0, sids[3].high);
}

static void test_nil_acl_skips_lookup(void)
{
    mock_attrs.default_acl.high = 0;
    mock_attrs.obj_flags[ACL_ATTR_OBJ_TYPE] = 1;
    sids[3].high = 0x999;
    ASSERT_EQ(status_$ok, run((int8_t)0xFF));
    ASSERT_EQ(0, slot_calls);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, sids[3].high);                 /* no slot: NIL */

    mock_attrs.obj_flags[ACL_ATTR_OBJ_TYPE] = 0; /* NIL but type 0: looked up */
    run(0);
    ASSERT_EQ(1, slot_calls);
}

static void test_slot_failure(void)
{
    mock_slot_status = 0x00230005;
    ASSERT_EQ(0x00230005, run((int8_t)0xFF));
    ASSERT_EQ(1, unlock_calls);
}

static void test_remote_object(void)
{
    mock_attrs.obj_flags[ACL_ATTR_FLAGS_LO] = 0;
    mock_remote = (int8_t)0x80;
    mock_rem_changed = (int8_t)0xFF;
    ASSERT_EQ(status_$acl_no_right_to_set_subsystem_data, run(0));
    ASSERT_EQ(1, rem_calls);
    ASSERT_EQ(0, lock_calls);

    ASSERT_EQ(status_$ok, run((int8_t)0xFF));
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)changed);

    ACL_$UNWIRED_DATA.local_locksmith = 1;    /* clears changed, status kept */
    ASSERT_EQ(status_$ok, run((int8_t)0xFF));
    ASSERT_EQ(0, changed);
}

int main(void)
{
    printf("acl_$setids tests:\n");
    RUN_TEST(attr_failure_sets_bit31);
    RUN_TEST(set_copies_setid_sids);
    RUN_TEST(check_mismatch);
    RUN_TEST(nil_required_uid_clears_slot3);
    RUN_TEST(nil_acl_skips_lookup);
    RUN_TEST(slot_failure);
    RUN_TEST(remote_object);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
