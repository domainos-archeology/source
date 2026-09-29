/*
 * acl/test/test_set_acl_check.c - Unit tests for ACL_$SET_ACL_CHECK
 * (0x00E470C4)
 *
 * acl/set_acl_check.c is #included below and driven through mocks of the
 * three acl_$ helpers, ACL_$RIGHTS, REM_FILE_$SET_ACL and ML_$LOCK /
 * ML_$UNLOCK.  Each test names the instruction it pins down.
 */

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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "acl/acl_internal.h"

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

uint16_t          PROC1_$CURRENT;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
acl_sid_block_t   ACL_$CURRENT_SIDS[PROC1_MAX_PROCESSES];
uid_t             ACL_$PROJ_UIDS[PROC1_MAX_PROCESSES][ACL_MAX_PROJECTS];
uint8_t           ACL_$LOCKSMITH_OVERRIDE_BITMAP[8];
uint8_t           ACL_$ASID_SUSER_BITMAP[8];
int16_t           ACL_$LOCAL_LOCKSMITH;
acl_$cache_slot_t ACL_$ACL_CACHE[ACL_CACHE_SLOTS];

uid_t UID_$NIL              = { 0, 0 };
uid_t RGYC_$G_LOCKSMITH_UID = { 0x00000542u, 0 };
uid_t ACL_$FILE_ACL         = { 0x00000601u, 0 };
uid_t ACL_$DIR_ACL          = { 0x00000602u, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static const uid_t OBJ_UID = { 0x11112222u, 0x33334444u };
static const uid_t ACL_UID = { 0x55556666u, 0x77778888u };
static const uid_t LOGIN   = { 0x0A0A0A0Au, 0x0B0B0B0Bu };
static const uid_t SUBSYS  = { 0x0C0C0C0Cu, 0x0D0D0D0Du };

/* --- acl_$get_obj_acl_attrs.  Call 1 is the object, call 2 the ACL. --- */
static int             ga_calls;
static ast_$acl_attr_t ga_attrs[2];
static file_$obj_loc_t ga_loc[2];
static status_$t       ga_status[2];
static uid_t           ga_uid_seen[2];

void acl_$get_obj_acl_attrs(uid_t *uid, file_$obj_loc_t *loc,
                            ast_$acl_attr_t *attrs, status_$t *status_ret)
{
    int i = ga_calls < 1 ? 0 : 1;
    ga_uid_seen[i] = *uid;
    ga_calls++;
    *loc   = ga_loc[i];
    *attrs = ga_attrs[i];
    *status_ret = ga_status[i];
}

/* --- acl_$find_acl_slot --- */
static int       fs_calls;
static int16_t   fs_result[2];
static status_$t fs_status[2];
static uid_t     fs_uid_seen[2];

int16_t acl_$find_acl_slot(uid_t *acl_uid, int8_t *cached_flag_ret,
                           acl_$prot_data_t *prot, status_$t *status_ret)
{
    int i = fs_calls < 1 ? 0 : 1;
    (void)prot;
    fs_uid_seen[i] = *acl_uid;
    fs_calls++;
    *cached_flag_ret = 0;
    *status_ret = fs_status[i];
    return fs_result[i];
}

uint16_t acl_$eval_acl_entries(acl_$cache_slot_t *slot, uid_t *sids,
                               uid_t *proj_uids, acl_$prot_data_t *prot)
{
    (void)slot; (void)sids; (void)proj_uids; (void)prot;
    return 0;
}

/* --- ACL_$RIGHTS --- */
static int       ar_calls;
static uid_t     ar_uid;
static boolean   ar_ignore_super;
static uint32_t  ar_mask;
static int16_t   ar_opts;
static uint32_t  ar_result;
static status_$t ar_status_out;

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status_ret)
{
    ar_calls++;
    ar_uid          = *uid;
    ar_ignore_super = *ignore_super;
    ar_mask         = *required_mask;
    ar_opts         = *option_flags;
    *status_ret     = ar_status_out;
    return ar_result;
}

/* --- REM_FILE_$SET_ACL --- */
static int       rs_calls;
static void     *rs_sid_data;
static void     *rs_perm_data;
static uint16_t  rs_flags;
static uint16_t  rs_extra;
static status_$t rs_status_out;

void REM_FILE_$SET_ACL(void *addr_info, uid_t *file_uid, uid_t *acl_uid,
                       void *acl_header, void *sid_data, void *perm_data,
                       uint16_t flags, uint16_t extra_flags,
                       status_$t *status)
{
    (void)addr_info; (void)file_uid; (void)acl_uid; (void)acl_header;
    rs_calls++;
    rs_sid_data  = sid_data;
    rs_perm_data = perm_data;
    rs_flags     = flags;
    rs_extra     = extra_flags;
    *status = rs_status_out;
}

/* --- ML --- */
static int ml_lock_calls, ml_unlock_calls, ml_last_id;
void ML_$LOCK(int16_t id)   { ml_lock_calls++;   ml_last_id = id; }
void ML_$UNLOCK(int16_t id) { ml_unlock_calls++; ml_last_id = id; }

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

#include "../set_acl_check.c"

/* ------------------------------------------------------------------ */

#define TEST_PID 5

static acl_$prot_data_t new_prot;
static boolean          setid_ret;

static void reset(void)
{
    memset(ga_attrs, 0, sizeof(ga_attrs));
    memset(ga_loc, 0, sizeof(ga_loc));
    memset(&new_prot, 0, sizeof(new_prot));
    memset(ACL_$CURRENT_SIDS, 0, sizeof(ACL_$CURRENT_SIDS));
    memset(ACL_$PROJ_UIDS, 0, sizeof(ACL_$PROJ_UIDS));
    memset(ACL_$LOCKSMITH_OVERRIDE_BITMAP, 0,
           sizeof(ACL_$LOCKSMITH_OVERRIDE_BITMAP));
    memset(ACL_$ASID_SUSER_BITMAP, 0, sizeof(ACL_$ASID_SUSER_BITMAP));
    memset(ACL_$ACL_CACHE, 0, sizeof(ACL_$ACL_CACHE));
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));

    ga_calls = fs_calls = ar_calls = rs_calls = 0;
    ml_lock_calls = ml_unlock_calls = 0; ml_last_id = -1;
    ga_status[0] = ga_status[1] = status_$ok;
    fs_status[0] = fs_status[1] = status_$ok;
    fs_result[0] = 0; fs_result[1] = 1;
    ar_status_out = status_$ok;
    ar_result = 0xFFFFFFFFu;
    rs_status_out = status_$ok;
    ACL_$LOCAL_LOCKSMITH = 0;
    PROC1_$CURRENT = TEST_PID;
    setid_ret = 0x7F;                       /* poison */

    /* Default: a local object whose ACL is held locally, both attribute
     * fetches succeed, the ACL object is of type 3. */
    ga_attrs[0].obj_flags[ACL_ATTR_FLAGS_LO]    = ACL_ATTR_FLAG_LOCAL;
    ga_attrs[0].obj_flags[ACL_ATTR_SUB_TYPE] = 5;
    ga_attrs[0].obj_flags[ACL_ATTR_OBJ_TYPE] = 1;
    ga_attrs[0].default_acl = UID_$NIL;
    ga_attrs[1].obj_flags[ACL_ATTR_SUB_TYPE] = 3;
    ga_attrs[1].obj_flags[ACL_ATTR_OBJ_TYPE] = 1;
    ga_attrs[1].default_acl = UID_$NIL;
    ACL_$CURRENT_SIDS[TEST_PID].login_sid = LOGIN;
}

static boolean call(int16_t op_type, status_$t *st_out)
{
    uid_t obj = OBJ_UID, acl = ACL_UID;
    status_$t st = 0xdeadbeef;
    boolean r = ACL_$SET_ACL_CHECK(&obj, &new_prot, &acl, &op_type,
                                   &setid_ret, &st);
    if (st_out) *st_out = st;
    return r;
}

/* ------------------------------------------------------------------ */

/*
 * 0x00E470DA-0x00E470E8: the UID is copied into the routine's own frame and
 * the set-id byte is cleared before anything else happens.
 */
TEST(prologue_copies_the_uid_and_clears_the_setid_byte)
{
    reset();
    ga_status[0] = 0x00120003;
    {
        status_$t st;
        ASSERT_EQ(0x00, (unsigned char)call(0, &st));
        ASSERT_EQ(0x80120003u, (uint32_t)st);
    }
    ASSERT_EQ(0x00, (unsigned char)setid_ret);
    ASSERT_EQ(OBJ_UID.high, ga_uid_seen[0].high);
    ASSERT_EQ(OBJ_UID.low,  ga_uid_seen[0].low);
    /* The copy, not the caller's pointer, is what is passed on. */
    ASSERT_EQ(1, ga_calls);
}

/*
 * 0x00E4710C-0x00E471BA: a remote object whose ACL is not held locally is
 * forwarded, with the per-process SID and project rows.
 */
TEST(remote_object_is_forwarded_with_the_project_count)
{
    reset();
    ga_attrs[0].obj_flags[ACL_ATTR_FLAGS_LO] = 0;
    ga_loc[0].flags = (int8_t)0x80;
    ACL_$PROJ_UIDS[TEST_PID][0].high = 1;
    ACL_$PROJ_UIDS[TEST_PID][1].high = 2;
    ACL_$PROJ_UIDS[TEST_PID][2] = UID_$NIL;

    ASSERT_EQ(0xFF, (unsigned char)call(7, NULL));
    ASSERT_EQ(1, rs_calls);
    ASSERT_TRUE(rs_sid_data  == (void *)&ACL_$CURRENT_SIDS[TEST_PID]);
    ASSERT_TRUE(rs_perm_data == (void *)&ACL_$PROJ_UIDS[TEST_PID][0]);
    ASSERT_EQ(2, rs_flags);             /* two non-NIL project slots */
    ASSERT_EQ(7, rs_extra);             /* *op_type */
    ASSERT_EQ(1, ga_calls);             /* the ACL object is never fetched */
    ASSERT_EQ(0, ml_lock_calls);

    /* No NIL slot at all -> the count stays 8 (0x00E4711C). */
    reset();
    ga_attrs[0].obj_flags[ACL_ATTR_FLAGS_LO] = 0;
    ga_loc[0].flags = (int8_t)0x80;
    {
        int i;
        for (i = 0; i < 8; i++) ACL_$PROJ_UIDS[TEST_PID][i].high = (uint32_t)(i + 1);
    }
    (void)call(0, NULL);
    ASSERT_EQ(8, rs_flags);

    /* A failed forward returns FALSE. */
    reset();
    ga_attrs[0].obj_flags[ACL_ATTR_FLAGS_LO] = 0;
    ga_loc[0].flags = (int8_t)0x80;
    rs_status_out = 0x000F0004;
    ASSERT_EQ(0x00, (unsigned char)call(0, NULL));
}

/*
 * 0x00E471D2-0x00E471F6: a missing ACL object is only tolerated when the
 * caller asked for the NIL ACL.
 */
TEST(missing_acl_object)
{
    reset();
    ga_status[1] = 0x000F0001;
    {
        status_$t st;
        (void)call(0, &st);
        ASSERT_EQ(0x800F0001u, (uint32_t)st);
    }

    /* Anything other than "object not found" also fails. */
    reset();
    ga_status[1] = 0x000F0002;
    {
        status_$t st;
        (void)call(0, &st);
        ASSERT_EQ(0x800F0002u, (uint32_t)st);
    }
}

/* 0x00E47200-0x00E47212: the ACL object must be of type 3. */
TEST(acl_object_type_must_be_three)
{
    reset();
    ga_attrs[1].obj_flags[ACL_ATTR_SUB_TYPE] = 4;
    {
        status_$t st;
        ASSERT_EQ(0x00, (unsigned char)call(0, &st));
        ASSERT_EQ(0x00230004u, (uint32_t)st);
    }
    ASSERT_EQ(0, ar_calls);
}

/*
 * source-u4vy: the ACL_$RIGHTS call at 0x00E47232 uses the two pea cells and
 * the OBJECT'S TYPE BYTE as its option-flags word.
 */
TEST(rights_check_uses_the_recovered_constants)
{
    reset();
    ga_attrs[0].obj_flags[ACL_ATTR_SUB_TYPE] = 5;
    (void)call(0, NULL);

    ASSERT_EQ(1, ar_calls);
    ASSERT_EQ(OBJ_UID.high, ar_uid.high);
    ASSERT_EQ(OBJ_UID.low,  ar_uid.low);
    ASSERT_EQ(0x00, (unsigned char)ar_ignore_super);
    ASSERT_EQ(0x0000000Fu, ar_mask);
    ASSERT_EQ(5, ar_opts);          /* the object type, not a constant */
}

/* 0x00E4723A-0x00E47252: only ok / 0x230001 / 0x230002 continue. */
TEST(rights_status_triage)
{
    reset();
    ar_status_out = 0x00230002;
    (void)call(0, NULL);
    ASSERT_EQ(1, ml_lock_calls);

    reset();
    ar_status_out = 0x00230004;
    ASSERT_EQ(0x00, (unsigned char)call(0, NULL));
    ASSERT_EQ(0, ml_lock_calls);
}

/*
 * 0x00E47256 / 0x00E4785E: the whole ACL comparison runs under ML lock 10 and
 * releases it on every exit.
 */
TEST(acl_comparison_runs_under_ml_lock_10)
{
    reset();
    (void)call(0, NULL);
    ASSERT_EQ(1, ml_lock_calls);
    ASSERT_EQ(1, ml_unlock_calls);
    ASSERT_EQ(ML_LOCK_ACL, ml_last_id);
    ASSERT_EQ(0x0A, ML_LOCK_ACL);
}

/*
 * 0x00E47264-0x00E472FE: a NIL default_acl together with a present
 * protection record means "no ACL image", so acl_$find_acl_slot is skipped.
 */
TEST(find_acl_slot_is_skipped_when_there_is_no_image)
{
    reset();
    (void)call(0, NULL);
    ASSERT_EQ(0, fs_calls);

    /* Give the object an ACL image and it is looked up. */
    reset();
    ga_attrs[0].default_acl = ACL_UID;
    (void)call(0, NULL);
    ASSERT_EQ(1, fs_calls);
    ASSERT_EQ(ACL_UID.high, fs_uid_seen[0].high);

    /* 0x00E472A0: 0x23000D is tolerated, anything else aborts. */
    reset();
    ga_attrs[0].default_acl = ACL_UID;
    fs_status[0] = 0x00230009;
    ASSERT_EQ(0x00, (unsigned char)call(0, NULL));
    ASSERT_EQ(1, ml_unlock_calls);
}

/*
 * 0x00E47310-0x00E473D6: a locksmith is granted outright and its ASID's
 * super bit is set.
 */
TEST(locksmith_is_granted_and_marked)
{
    reset();
    ACL_$CURRENT_SIDS[TEST_PID].login_sid = RGYC_$G_LOCKSMITH_UID;
    {
        status_$t st;
        ASSERT_EQ(0xFF, (unsigned char)call(0, &st));
        ASSERT_EQ(status_$ok, (uint32_t)st);
    }
    ASSERT_EQ(ACL_PID_BITMAP_MASK(TEST_PID),
              ACL_$ASID_SUSER_BITMAP[ACL_PID_BITMAP_BYTE(TEST_PID)]);

    /* The local-locksmith downgrade takes it away from a type-9 process. */
    reset();
    ACL_$CURRENT_SIDS[TEST_PID].login_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$LOCAL_LOCKSMITH = 1;
    PROC1_$DATA.type[TEST_PID] = 9;
    ar_result = 0;                      /* and no change right either */
    {
        status_$t st;
        ASSERT_EQ(0x00, (unsigned char)call(0, &st));
        ASSERT_EQ(0x00230001u, (uint32_t)st);
    }
    ASSERT_EQ(0, ACL_$ASID_SUSER_BITMAP[ACL_PID_BITMAP_BYTE(TEST_PID)]);

    /* ...unless the override bit is set. */
    reset();
    ACL_$CURRENT_SIDS[TEST_PID].login_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$LOCAL_LOCKSMITH = 1;
    PROC1_$DATA.type[TEST_PID] = 9;
    ACL_$LOCKSMITH_OVERRIDE_BITMAP[ACL_PID_BITMAP_BYTE(TEST_PID)] |=
        ACL_PID_BITMAP_MASK(TEST_PID);
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));
}

/*
 * 0x00E473DA-0x00E473F8: without the change right (bit 3) the answer is
 * "no right to perform operation", and a missing ACL object outranks it.
 */
TEST(change_right_and_missing_acl)
{
    reset();
    ar_result = 0;
    {
        status_$t st;
        ASSERT_EQ(0x00, (unsigned char)call(0, &st));
        ASSERT_EQ(0x00230001u, (uint32_t)st);
    }

    reset();
    ga_attrs[0].default_acl = ACL_UID;
    fs_status[0] = status_$acl_object_not_found;
    {
        status_$t st;
        ASSERT_EQ(0x00, (unsigned char)call(0, &st));
        ASSERT_EQ(0x0023000Du, (uint32_t)st);
    }
}

/*
 * 0x00E4770E: operation type 5 skips every SID check, and with neither object
 * carrying an ACL image the answer is a plain grant.
 */
TEST(op_type_five_grants_without_sid_checks)
{
    reset();
    new_prot.owner_rights = ACL_PROT_SETID_BIT;
    {
        status_$t st;
        ASSERT_EQ(0xFF, (unsigned char)call(ACL_SET_OP_NO_SID_CHECK, &st));
        ASSERT_EQ(status_$ok, (uint32_t)st);
    }
}

/*
 * 0x00E47716-0x00E47738: with no SID being changed the operation is granted
 * without looking at the caller's identity.
 */
TEST(no_sid_change_is_granted)
{
    reset();
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));
    ASSERT_EQ(0x00, (unsigned char)setid_ret);
}

/*
 * 0x00E4775C-0x00E4779C: changing the owner SID sets the set-id byte and
 * requires the new SID to be the caller's own user SID.
 */
TEST(owner_sid_change_must_match_the_callers_own)
{
    reset();
    new_prot.owner_rights = ACL_PROT_SETID_BIT;
    new_prot.owner = SUBSYS;
    ACL_$CURRENT_SIDS[TEST_PID].user_sid = SUBSYS;
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));
    ASSERT_EQ(0xFF, (unsigned char)setid_ret);

    reset();
    new_prot.owner_rights = ACL_PROT_SETID_BIT;
    new_prot.owner = SUBSYS;
    ACL_$CURRENT_SIDS[TEST_PID].user_sid = LOGIN;
    ASSERT_EQ(0x00, (unsigned char)call(0, NULL));
    ASSERT_EQ(0xFF, (unsigned char)setid_ret);      /* still set at 0x00E4775C */

    /* 0x00E4776A: op type 3 takes the SID from the OBJECT's own record. */
    reset();
    new_prot.owner_rights = ACL_PROT_SETID_BIT;
    new_prot.owner = SUBSYS;                        /* would not match */
    ACL_$PROT_DATA(&ga_attrs[0])->owner = LOGIN;
    ACL_$CURRENT_SIDS[TEST_PID].user_sid = LOGIN;
    ASSERT_EQ(0xFF, (unsigned char)call(ACL_SET_OP_MERGE, NULL));
}

/*
 * 0x00E4773A-0x00E47756: a type-9 process may not set-id at all while
 * ACL_$LOCAL_LOCKSMITH is in force.
 */
TEST(type_nine_process_may_not_setid_under_local_locksmith)
{
    reset();
    new_prot.owner_rights = ACL_PROT_SETID_BIT;
    ACL_$LOCAL_LOCKSMITH = 1;
    PROC1_$DATA.type[TEST_PID] = 9;
    ASSERT_EQ(0x00, (unsigned char)call(0, NULL));
    ASSERT_EQ(0x00, (unsigned char)setid_ret);

    /* A non-type-9 process is unaffected. */
    reset();
    new_prot.owner_rights = ACL_PROT_SETID_BIT;
    new_prot.owner = SUBSYS;
    ACL_$CURRENT_SIDS[TEST_PID].user_sid = SUBSYS;
    ACL_$LOCAL_LOCKSMITH = 1;
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));
}

/*
 * 0x00E4779E-0x00E4781E: a group SID change is allowed when the new group is
 * the caller's own, or one of its eight project UIDs; a NIL slot ends the
 * list.
 */
TEST(group_sid_change_scans_the_project_list)
{
    reset();
    new_prot.group_rights = ACL_PROT_SETID_BIT;
    new_prot.group = SUBSYS;
    ACL_$CURRENT_SIDS[TEST_PID].group_sid = SUBSYS;
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));

    reset();
    new_prot.group_rights = ACL_PROT_SETID_BIT;
    new_prot.group = SUBSYS;
    ACL_$PROJ_UIDS[TEST_PID][0] = LOGIN;
    ACL_$PROJ_UIDS[TEST_PID][1] = SUBSYS;
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));

    /* A NIL slot before the match stops the walk. */
    reset();
    new_prot.group_rights = ACL_PROT_SETID_BIT;
    new_prot.group = SUBSYS;
    ACL_$PROJ_UIDS[TEST_PID][0] = LOGIN;
    ACL_$PROJ_UIDS[TEST_PID][1] = UID_$NIL;
    ACL_$PROJ_UIDS[TEST_PID][2] = SUBSYS;
    ASSERT_EQ(0x00, (unsigned char)call(0, NULL));
}

/* 0x00E47820-0x00E47850: the organisation SID must be the caller's own. */
TEST(org_sid_change_must_match)
{
    reset();
    new_prot.org_rights = ACL_PROT_SETID_BIT;
    new_prot.org = SUBSYS;
    ACL_$CURRENT_SIDS[TEST_PID].org_sid = SUBSYS;
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));

    reset();
    new_prot.org_rights = ACL_PROT_SETID_BIT;
    new_prot.org = SUBSYS;
    ASSERT_EQ(0x00, (unsigned char)call(0, NULL));
}

/*
 * 0x00E47454-0x00E4746E: when both objects carry an ACL image, the two ACLs
 * must protect the same kind of object.
 */
TEST(both_acl_images_must_agree_on_the_type)
{
    reset();
    ga_attrs[0].default_acl = ACL_UID;
    ga_attrs[1].default_acl = ACL_UID;
    fs_result[0] = 2; fs_result[1] = 3;
    ACL_$ACL_CACHE[2].type_uid = ACL_$FILE_ACL;
    ACL_$ACL_CACHE[3].type_uid = ACL_$DIR_ACL;
    {
        status_$t st;
        ASSERT_EQ(0x00, (unsigned char)call(0, &st));
        ASSERT_EQ(0x00230004u, (uint32_t)st);
    }

    /* Same type, same subsystem and required UIDs -> granted. */
    reset();
    ga_attrs[0].default_acl = ACL_UID;
    ga_attrs[1].default_acl = ACL_UID;
    fs_result[0] = 2; fs_result[1] = 3;
    ACL_$ACL_CACHE[2].type_uid = ACL_$FILE_ACL;
    ACL_$ACL_CACHE[3].type_uid = ACL_$FILE_ACL;
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));
    ASSERT_EQ(0x00, (unsigned char)setid_ret);
}

/*
 * 0x00E4747E-0x00E474E4: the subsystem manager may only change between "no
 * subsystem" (subsys == type) and the caller's own login SID, and doing so
 * sets the set-id byte.
 */
TEST(subsystem_manager_handover)
{
    /* s1 has no subsystem, s2 names the caller -> allowed, set-id. */
    reset();
    ga_attrs[0].default_acl = ACL_UID;
    ga_attrs[1].default_acl = ACL_UID;
    fs_result[0] = 2; fs_result[1] = 3;
    ACL_$ACL_CACHE[2].type_uid   = ACL_$FILE_ACL;
    ACL_$ACL_CACHE[3].type_uid   = ACL_$FILE_ACL;
    ACL_$ACL_CACHE[2].subsys_uid = ACL_$FILE_ACL;   /* == its own type */
    ACL_$ACL_CACHE[3].subsys_uid = LOGIN;
    ASSERT_EQ(0xFF, (unsigned char)call(0, NULL));
    ASSERT_EQ(0xFF, (unsigned char)setid_ret);

    /* Neither side is the caller -> refused with 0x230010. */
    reset();
    ga_attrs[0].default_acl = ACL_UID;
    ga_attrs[1].default_acl = ACL_UID;
    fs_result[0] = 2; fs_result[1] = 3;
    ACL_$ACL_CACHE[2].type_uid   = ACL_$FILE_ACL;
    ACL_$ACL_CACHE[3].type_uid   = ACL_$FILE_ACL;
    ACL_$ACL_CACHE[2].subsys_uid = SUBSYS;
    ACL_$ACL_CACHE[3].subsys_uid = ACL_UID;
    {
        status_$t st;
        ASSERT_EQ(0x00, (unsigned char)call(0, &st));
        ASSERT_EQ(0x00230010u, (uint32_t)st);
    }
}

int main(void)
{
    printf("ACL_$SET_ACL_CHECK (0x00E470C4) tests\n");

    RUN_TEST(prologue_copies_the_uid_and_clears_the_setid_byte);
    RUN_TEST(remote_object_is_forwarded_with_the_project_count);
    RUN_TEST(missing_acl_object);
    RUN_TEST(acl_object_type_must_be_three);
    RUN_TEST(rights_check_uses_the_recovered_constants);
    RUN_TEST(rights_status_triage);
    RUN_TEST(acl_comparison_runs_under_ml_lock_10);
    RUN_TEST(find_acl_slot_is_skipped_when_there_is_no_image);
    RUN_TEST(locksmith_is_granted_and_marked);
    RUN_TEST(change_right_and_missing_acl);
    RUN_TEST(op_type_five_grants_without_sid_checks);
    RUN_TEST(no_sid_change_is_granted);
    RUN_TEST(owner_sid_change_must_match_the_callers_own);
    RUN_TEST(type_nine_process_may_not_setid_under_local_locksmith);
    RUN_TEST(group_sid_change_scans_the_project_list);
    RUN_TEST(org_sid_change_must_match);
    RUN_TEST(both_acl_images_must_agree_on_the_type);
    RUN_TEST(subsystem_manager_handover);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
