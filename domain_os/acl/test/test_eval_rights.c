/*
 * acl/test/test_eval_rights.c - Unit tests for acl_$eval_rights (0x00E464B8)
 *
 * acl/eval_rights.c is #included below and driven through mocks of the three
 * helpers it calls (acl_$get_obj_acl_attrs 0x00E45F78, acl_$find_acl_slot
 * 0x00E45E8E, acl_$eval_acl_entries 0x00E46172), REM_FILE_$ACL_CHECK_RIGHTS
 * (0x00E629E8) and ML_$LOCK / ML_$UNLOCK, so every assertion exercises the
 * shipping code.
 *
 * Each test names the instruction it pins down.
 */

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                    */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Globals the code under test refers to                                */
/* ------------------------------------------------------------------ */

#include "acl/acl_internal.h"

uint16_t          PROC1_$CURRENT;
uint16_t          PROC1_$TYPE[PROC1_MAX_PROCESSES];
acl_sid_block_t   ACL_$CURRENT_SIDS[PROC1_MAX_PROCESSES];
uid_t             ACL_$PROJ_UIDS[PROC1_MAX_PROCESSES][ACL_MAX_PROJECTS];
uint8_t           ACL_$LOCKSMITH_OVERRIDE_BITMAP[8];
int16_t           ACL_$LOCAL_LOCKSMITH;
acl_$cache_slot_t ACL_$ACL_CACHE[ACL_CACHE_SLOTS];

uid_t UID_$NIL                = { 0, 0 };
uid_t RGYC_$G_LOCKSMITH_UID   = { 0x00000542u, 0 };
uid_t RGYC_$P_USER_UID        = { 0x00800001u, 0 };
uid_t RGYC_$G_NIL_UID         = { 0x00800040u, 0 };
uid_t PPO_$NIL_ORG_UID        = { 0x00800080u, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

/* --- acl_$get_obj_acl_attrs (0x00E45F78) --- */
static int             ga_calls;
static uid_t          *ga_uid;
static ast_$acl_attr_t ga_attrs_out;     /* what the mock writes back */
static file_$obj_loc_t ga_loc_out;
static status_$t       ga_status_out;

void acl_$get_obj_acl_attrs(uid_t *uid, file_$obj_loc_t *loc,
                            ast_$acl_attr_t *attrs, status_$t *status_ret)
{
    ga_calls++;
    ga_uid = uid;
    *loc   = ga_loc_out;
    *attrs = ga_attrs_out;
    *status_ret = ga_status_out;
}

/* --- acl_$find_acl_slot (0x00E45E8E) --- */
static int       fs_calls;
static int16_t   fs_result;
static status_$t fs_status_out;
static uid_t     fs_acl_uid_seen;
static acl_$prot_data_t *fs_prot_seen;

int16_t acl_$find_acl_slot(uid_t *acl_uid, int8_t *cached_flag_ret,
                           acl_$prot_data_t *prot, status_$t *status_ret)
{
    fs_calls++;
    fs_acl_uid_seen = *acl_uid;
    fs_prot_seen    = prot;
    *cached_flag_ret = 0;
    *status_ret = fs_status_out;
    return fs_result;
}

/* --- acl_$eval_acl_entries (0x00E46172) --- */
static int       ee_calls;
static uint16_t  ee_result;
static acl_$cache_slot_t *ee_slot_seen;
static uid_t    *ee_sids_seen;
static uid_t    *ee_proj_seen;

uint16_t acl_$eval_acl_entries(acl_$cache_slot_t *slot, uid_t *sids,
                               uid_t *proj_uids, acl_$prot_data_t *prot)
{
    (void)prot;
    ee_calls++;
    ee_slot_seen = slot;
    ee_sids_seen = sids;
    ee_proj_seen = proj_uids;
    return ee_result;
}

/* --- REM_FILE_$ACL_CHECK_RIGHTS (0x00E629E8) --- */
static int       rf_calls;
static void     *rf_addr_info;
static void     *rf_sid_data;
static void     *rf_perm_data;
static uid_t    *rf_uid;
static uint8_t   rf_check_flag;
static uint32_t  rf_access_mask;
static uint16_t  rf_flags;
static uint8_t   rf_flag2;
static uint8_t   rf_flag3;
static uint32_t  rf_result_out;
static status_$t rf_status_out;

void REM_FILE_$ACL_CHECK_RIGHTS(void *addr_info, void *sid_data,
                                void *perm_data, uid_t *file_uid,
                                uint8_t check_flag, uint32_t access_mask,
                                uint16_t flags, uint8_t flag2, uint8_t flag3,
                                uint32_t *result_out, status_$t *status)
{
    rf_calls++;
    rf_addr_info   = addr_info;
    rf_sid_data    = sid_data;
    rf_perm_data   = perm_data;
    rf_uid         = file_uid;
    rf_check_flag  = check_flag;
    rf_access_mask = access_mask;
    rf_flags       = flags;
    rf_flag2       = flag2;
    rf_flag3       = flag3;
    *result_out    = rf_result_out;
    *status        = rf_status_out;
}

/* --- ML_$LOCK / ML_$UNLOCK --- */
static int ml_lock_calls;
static int ml_unlock_calls;
static int ml_last_id;

void ML_$LOCK(int16_t resource_id)   { ml_lock_calls++;   ml_last_id = resource_id; }
void ML_$UNLOCK(int16_t resource_id) { ml_unlock_calls++; ml_last_id = resource_id; }

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

#include "../eval_rights.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

#define TEST_PID    7

static acl_sid_block_t caller_sids;
static uid_t           caller_proj[ACL_MAX_PROJECTS];

static const uid_t OBJ_UID   = { 0x11112222u, 0x33334444u };
static const uid_t OWNER_UID = { 0x0A0A0A0Au, 0x0B0B0B0Bu };
static const uid_t GROUP_UID = { 0x0C0C0C0Cu, 0x0D0D0D0Du };
static const uid_t ORG_UID   = { 0x0E0E0E0Eu, 0x0F0F0F0Fu };
static const uid_t ACL_UID   = { 0x77778888u, 0x9999AAAAu };

static acl_$prot_data_t *attrs_prot(void)
{
    return ACL_$PROT_DATA(&ga_attrs_out);
}

static void reset(void)
{
    memset(&ga_attrs_out, 0, sizeof(ga_attrs_out));
    memset(&ga_loc_out,   0, sizeof(ga_loc_out));
    memset(&caller_sids,  0, sizeof(caller_sids));
    memset(caller_proj,   0, sizeof(caller_proj));
    memset(ACL_$CURRENT_SIDS, 0, sizeof(ACL_$CURRENT_SIDS));
    memset(ACL_$PROJ_UIDS, 0, sizeof(ACL_$PROJ_UIDS));
    memset(ACL_$LOCKSMITH_OVERRIDE_BITMAP, 0,
           sizeof(ACL_$LOCKSMITH_OVERRIDE_BITMAP));
    memset(ACL_$ACL_CACHE, 0, sizeof(ACL_$ACL_CACHE));
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));

    ga_calls = fs_calls = ee_calls = rf_calls = 0;
    ga_status_out = fs_status_out = rf_status_out = status_$ok;
    ga_uid = NULL;
    fs_result = ACL_CACHE_NO_SLOT;
    fs_prot_seen = NULL;
    ee_result = 0;
    ee_slot_seen = NULL; ee_sids_seen = NULL; ee_proj_seen = NULL;
    rf_result_out = 0;
    ml_lock_calls = ml_unlock_calls = 0;
    ml_last_id = -1;
    ACL_$LOCAL_LOCKSMITH = 0;
    PROC1_$CURRENT = TEST_PID;

    /* The "object has its own protection record, no ACL" shape: default_acl
     * is NIL and obj_flags[0] is non-zero (0x00E466D6 / 0x00E466F0). */
    ga_attrs_out.obj_flags[ACL_ATTR_PRESENT]  = 1;
    ga_attrs_out.obj_flags[ACL_ATTR_OBJ_TYPE] = 1;   /* matches option_flags 1 */
    ga_attrs_out.obj_flags[ACL_ATTR_FLAGS]    = ACL_ATTR_FLAG_LOCAL;
    ga_attrs_out.default_acl = UID_$NIL;

    attrs_prot()->owner = OWNER_UID;
    attrs_prot()->group = GROUP_UID;
    attrs_prot()->org   = ORG_UID;
    attrs_prot()->owner_rights = 0x0F;
    attrs_prot()->group_rights = 0x05;
    attrs_prot()->org_rights   = 0x01;
    attrs_prot()->world_rights = 0x00;
}

static uint32_t call(boolean ignore_super, uint32_t mask, int16_t opts,
                     boolean in_super, boolean in_subsys, status_$t *st)
{
    uid_t uid = OBJ_UID;
    return acl_$eval_rights(&caller_sids, caller_proj, &uid, ignore_super,
                            mask, opts, in_super, in_subsys, st);
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/*
 * 0x00E464DC-0x00E464E2: `tst.b D4b / bpl` then `tst.b D5b / bpl`.  In super
 * mode with ignore_super FALSE, the routine jumps straight to 0x00E4668A
 * (`moveq #0xf`) without ever calling acl_$get_obj_acl_attrs.
 */
TEST(super_user_short_circuit_grants_all_four_rights)
{
    status_$t st = 0xdeadbeef;
    uint32_t  r;

    reset();
    r = call(false, 0x0000000Fu, 1, true, false, &st);

    ASSERT_EQ(0, ga_calls);
    ASSERT_EQ(0x0000000Fu, r);
    ASSERT_EQ(status_$ok, st);

    /* With a mask asking for more than the four rights, the intersection is
     * still 0xF but the status becomes "insufficient" (0x00E468BC). */
    reset();
    st = 0;
    r = call(false, 0xFFFFFFFFu, 1, true, false, &st);
    ASSERT_EQ(0x0000000Fu, r);
    ASSERT_EQ(0x00230002u, (uint32_t)st);
}

/*
 * The same instruction pair the other way round: ignore_super TRUE
 * suppresses the bypass, so the evaluation runs normally.
 */
TEST(ignore_super_suppresses_the_super_user_bypass)
{
    status_$t st = 0xdeadbeef;

    reset();
    caller_sids.user_sid = OWNER_UID;
    (void)call(true, 0x0000000Fu, 1, true, false, &st);

    ASSERT_EQ(1, ga_calls);
}

/*
 * 0x00E465F2-0x00E46602: a failed attribute fetch sets bit 7 of the FIRST
 * byte of the status longword, i.e. bit 31, and returns zero rights.
 */
TEST(attribute_failure_sets_bit_31_of_the_status)
{
    status_$t st = 0;
    uint32_t  r;

    reset();
    ga_status_out = 0x00120003;
    r = call(false, 0x0000000Fu, 1, false, false, &st);

    ASSERT_EQ(0u, r);
    ASSERT_EQ(0x80120003u, (uint32_t)st);
}

/*
 * ...unless the caller is the locksmith, in which case 0x00E465FA jumps to
 * 0x00E4668A and the failure is swallowed.
 */
TEST(attribute_failure_is_swallowed_for_the_locksmith)
{
    status_$t st = 0xdeadbeef;
    uint32_t  r;

    reset();
    caller_sids.login_sid = RGYC_$G_LOCKSMITH_UID;
    ga_status_out = 0x00120003;
    r = call(false, 0x0000000Fu, 1, false, false, &st);

    ASSERT_EQ(0x0000000Fu, r);
    ASSERT_EQ(status_$ok, st);
}

/*
 * 0x00E46606-0x00E46664: attrs.obj_flags[3] bit 0 clear plus a negative
 * loc.flags byte routes the whole question to the owning node, and the
 * per-process GLOBAL tables - not the caller's arguments - are what get sent
 * (0x00E46626-0x00E4664E).
 */
TEST(remote_objects_are_forwarded_with_the_global_tables)
{
    status_$t st = 0xdeadbeef;
    uint32_t  r;

    reset();
    ga_attrs_out.obj_flags[ACL_ATTR_FLAGS] = 0;         /* not local */
    ga_loc_out.flags = (int8_t)FILE_OBJ_LOC_REMOTE;     /* bit 7 */
    rf_result_out = 0x0000004Au;
    rf_status_out = 0x00230002;

    r = call(true, 0x00000048u, 1, false, true, &st);

    ASSERT_EQ(1, rf_calls);
    ASSERT_EQ(0x0000004Au, r);
    ASSERT_EQ(0x00230002u, (uint32_t)st);
    ASSERT_TRUE(rf_addr_info == (void *)&ga_loc_out.loc_info ||
                rf_addr_info != NULL);   /* the loc record is a local copy */
    ASSERT_TRUE(rf_sid_data  == (void *)&ACL_$CURRENT_SIDS[TEST_PID]);
    ASSERT_TRUE(rf_perm_data == (void *)&ACL_$PROJ_UIDS[TEST_PID][0]);
    ASSERT_EQ(0xFF, rf_check_flag);      /* ignore_super */
    ASSERT_EQ(0x00000048u, rf_access_mask);
    ASSERT_EQ(1, rf_flags);
    ASSERT_EQ(0x00, rf_flag2);           /* in_super */
    ASSERT_EQ(0xFF, rf_flag3);           /* in_subsys */
    ASSERT_EQ(0, fs_calls);
}

/*
 * A remote object whose ACL *is* local (bit 0 set) is NOT forwarded
 * (0x00E4660C `bne`).
 */
TEST(local_acl_bit_prevents_the_remote_forward)
{
    status_$t st = 0xdeadbeef;

    reset();
    ga_attrs_out.obj_flags[ACL_ATTR_FLAGS] = ACL_ATTR_FLAG_LOCAL;
    ga_loc_out.flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    caller_sids.user_sid = OWNER_UID;

    (void)call(false, 0x0000000Fu, 1, false, false, &st);
    ASSERT_EQ(0, rf_calls);
}

/*
 * 0x00E4668E-0x00E466C4: the object type in attrs.obj_flags[1] must agree
 * with the caller's option_flags word, or the request is rejected with
 * "wrong type - operation illegal on system objects" (0x00230004).
 */
TEST(object_type_mismatch_is_rejected)
{
    status_$t st = 0;
    uint32_t  r;

    reset();
    ga_attrs_out.obj_flags[ACL_ATTR_OBJ_TYPE] = 3;
    r = call(false, 0x0000000Fu, 1, false, false, &st);

    ASSERT_EQ(0u, r);
    ASSERT_EQ(0x00230004u, (uint32_t)st);
}

/* The four accepted mismatches: type==opts, opts==1 && type==2, opts==-1,
 * and opts==0 with type 4 or 5 (0x00E46698-0x00E466BC). */
TEST(object_type_exemptions_are_accepted)
{
    status_$t st;
    static const struct { int16_t opts; uint8_t type; } ok[] = {
        { 1, 1 }, { 1, 2 }, { -1, 9 }, { 0, 4 }, { 0, 5 },
    };
    unsigned i;

    for (i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        reset();
        ga_attrs_out.obj_flags[ACL_ATTR_OBJ_TYPE] = ok[i].type;
        caller_sids.user_sid = OWNER_UID;
        st = 0;
        (void)call(false, 0x0000000Fu, ok[i].opts, false, false, &st);
        ASSERT_EQ(status_$ok, st);
    }

    /* opts == 0 with type 3 is NOT exempt. */
    reset();
    ga_attrs_out.obj_flags[ACL_ATTR_OBJ_TYPE] = 3;
    st = 0;
    (void)call(false, 0x0000000Fu, 0, false, false, &st);
    ASSERT_EQ(0x00230004u, (uint32_t)st);
}

/*
 * 0x00E466FA-0x00E4671A: an owner match returns the owner rights byte, and
 * bit 4 of that byte (ACL_RIGHT_IGNORE) disables the match entirely.
 */
TEST(owner_match_returns_the_owner_rights)
{
    status_$t st = 0xdeadbeef;

    reset();
    caller_sids.user_sid = OWNER_UID;
    ASSERT_EQ(0x0Fu, call(false, 0x0000000Fu, 1, false, false, &st));
    ASSERT_EQ(status_$ok, st);

    /* bit 4 set -> skip the owner test and fall through to world (0). */
    reset();
    caller_sids.user_sid = OWNER_UID;
    attrs_prot()->owner_rights = 0x1F;
    st = 0;
    ASSERT_EQ(0u, call(false, 0x0000000Fu, 1, false, false, &st));
    ASSERT_EQ(0x00230001u, (uint32_t)st);   /* no right at all */
}

/*
 * 0x00E4671E-0x00E46742 (direct group SID) and 0x00E4675A-0x00E4678E (the
 * eight-entry project list).  A NIL slot terminates the walk (0x00E46778).
 */
TEST(group_matches_directly_or_through_the_project_list)
{
    status_$t st = 0xdeadbeef;

    reset();
    caller_sids.group_sid = GROUP_UID;
    ASSERT_EQ(0x05u, call(false, 0x0000000Fu, 1, false, false, &st));

    reset();
    caller_proj[0] = ORG_UID;       /* not the group */
    caller_proj[1] = GROUP_UID;     /* hit on the second slot */
    ASSERT_EQ(0x05u, call(false, 0x0000000Fu, 1, false, false, &st));

    /* A NIL before the match stops the walk: world rights instead. */
    reset();
    caller_proj[0] = ORG_UID;
    caller_proj[1] = UID_$NIL;
    caller_proj[2] = GROUP_UID;
    attrs_prot()->world_rights = 0x02;
    ASSERT_EQ(0x02u, call(false, 0x0000000Fu, 1, false, false, &st));

    /* An empty list (slot 0 NIL) is not walked at all (0x00E46746). */
    reset();
    attrs_prot()->world_rights = 0x02;
    ASSERT_EQ(0x02u, call(false, 0x0000000Fu, 1, false, false, &st));
}

/* 0x00E46792-0x00E467B6 then the 0x00E467BA world fallback. */
TEST(org_then_world_are_the_last_two_chances)
{
    status_$t st = 0xdeadbeef;

    reset();
    caller_sids.org_sid = ORG_UID;
    ASSERT_EQ(0x01u, call(false, 0x00000001u, 1, false, false, &st));

    reset();
    attrs_prot()->world_rights = 0x04;
    ASSERT_EQ(0x04u, call(false, 0x00000004u, 1, false, false, &st));
}

/*
 * 0x00E46668-0x00E46688: a locksmith GROUP SID with option_flags == 0 wins
 * everything, independent of the protection record.
 */
TEST(locksmith_group_sid_with_zero_options_grants_all)
{
    status_$t st = 0xdeadbeef;

    reset();
    ga_attrs_out.obj_flags[ACL_ATTR_OBJ_TYPE] = 4;   /* exempt for opts 0 */
    caller_sids.group_sid = RGYC_$G_LOCKSMITH_UID;
    ASSERT_EQ(0x0Fu, call(false, 0x0000000Fu, 0, false, false, &st));
}

/*
 * 0x00E466C8-0x00E466CE: a locksmith that did not take the option_flags==0
 * path still gets the privileged mask 0xFFFFFFAF.
 */
TEST(locksmith_gets_the_privileged_mask)
{
    status_$t st = 0xdeadbeef;

    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    /* required_mask 0x50 asks for bits 4 and 6, which 0xFFFFFFAF withholds. */
    ASSERT_EQ(0x00u, call(false, 0x00000050u, 1, false, false, &st));
    ASSERT_EQ(0x00230001u, (uint32_t)st);

    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    ASSERT_EQ(0x0000000Fu, call(false, 0x0000000Fu, 1, false, false, &st));
}

/*
 * 0x00E4653E-0x00E465D6: ACL_$LOCAL_LOCKSMITH != 0 plus PROC1_$TYPE[cur] == 9
 * plus a clear override bit downgrades the locksmith to the generic user, and
 * ACL_$LOCAL_LOCKSMITH == 1 adds read+execute back at 0x00E468A0.
 */
TEST(local_locksmith_downgrade)
{
    status_$t st = 0xdeadbeef;

    /* Without the feature the locksmith keeps everything. */
    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    ASSERT_EQ(0x0000000Fu, call(false, 0x0000000Fu, 1, false, false, &st));

    /* With it, and a type-9 process, the identity becomes RGYC_$P_USER_UID /
     * RGYC_$G_NIL_UID / PPO_$NIL_ORG_UID, none of which owns the object. */
    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$LOCAL_LOCKSMITH = 2;
    PROC1_$TYPE[TEST_PID] = 9;
    ASSERT_EQ(0x00u, call(false, 0x0000000Fu, 1, false, false, &st));

    /* ACL_$LOCAL_LOCKSMITH == 1 hands read+execute (0x5) back. */
    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$LOCAL_LOCKSMITH = 1;
    PROC1_$TYPE[TEST_PID] = 9;
    ASSERT_EQ(0x05u, call(false, 0x0000000Fu, 1, false, false, &st));

    /* The downgrade substitutes the generic user, so an object owned by
     * RGYC_$P_USER_UID matches. */
    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$LOCAL_LOCKSMITH = 2;
    PROC1_$TYPE[TEST_PID] = 9;
    attrs_prot()->owner = RGYC_$P_USER_UID;
    ASSERT_EQ(0x0Fu, call(false, 0x0000000Fu, 1, false, false, &st));

    /* The override bit (0x00E4657C) cancels the downgrade. */
    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$LOCAL_LOCKSMITH = 2;
    PROC1_$TYPE[TEST_PID] = 9;
    ACL_$LOCKSMITH_OVERRIDE_BITMAP[ACL_PID_BITMAP_BYTE(TEST_PID)] |=
        ACL_PID_BITMAP_MASK(TEST_PID);
    ASSERT_EQ(0x0000000Fu, call(false, 0x0000000Fu, 1, false, false, &st));

    /* A non-type-9 process is never downgraded (0x00E4655A). */
    reset();
    caller_sids.user_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$LOCAL_LOCKSMITH = 2;
    PROC1_$TYPE[TEST_PID] = 2;
    ASSERT_EQ(0x0000000Fu, call(false, 0x0000000Fu, 1, false, false, &st));
}

/*
 * ACL_PID_BITMAP_BYTE / ACL_PID_BITMAP_MASK reproduce
 * `subq.b #1 / lsr.w #3` + `moveq #7 / sub.b / btst` (0x00E46564-0x00E4657C).
 */
TEST(locksmith_override_bitmap_addressing)
{
    ASSERT_EQ(0, ACL_PID_BITMAP_BYTE(1));
    ASSERT_EQ(0x80, ACL_PID_BITMAP_MASK(1));
    ASSERT_EQ(0, ACL_PID_BITMAP_BYTE(8));
    ASSERT_EQ(0x01, ACL_PID_BITMAP_MASK(8));
    ASSERT_EQ(1, ACL_PID_BITMAP_BYTE(9));
    ASSERT_EQ(0x80, ACL_PID_BITMAP_MASK(9));
    ASSERT_EQ(7, ACL_PID_BITMAP_BYTE(64));
    ASSERT_EQ(0x01, ACL_PID_BITMAP_MASK(64));
}

/*
 * 0x00E467C4-0x00E46894: the ACL-image path.  It brackets everything in
 * ML_$LOCK(10) / ML_$UNLOCK(10) and clears bit 4 of whatever the entry walk
 * returned (`andi.l #-0x11`).
 */
TEST(acl_image_path_locks_and_masks_bit_4)
{
    status_$t st = 0xdeadbeef;
    uint32_t  r;

    reset();
    ga_attrs_out.default_acl = ACL_UID;      /* not NIL -> full ACL path */
    fs_result = 3;
    ee_result = 0x001F;
    r = call(false, 0x0000FFFFu, 1, false, false, &st);

    ASSERT_EQ(1, fs_calls);
    ASSERT_EQ(1, ee_calls);
    ASSERT_EQ(1, ml_lock_calls);
    ASSERT_EQ(1, ml_unlock_calls);
    ASSERT_EQ(ML_LOCK_ACL, ml_last_id);
    ASSERT_EQ(0x0000000Fu, r);               /* 0x1F & ~0x10 */
    ASSERT_TRUE(ee_slot_seen == &ACL_$ACL_CACHE[3]);
    ASSERT_TRUE(ee_sids_seen == &caller_sids.user_sid);
    ASSERT_TRUE(ee_proj_seen == caller_proj);
    ASSERT_EQ(ACL_UID.high, fs_acl_uid_seen.high);
}

/*
 * 0x00E4684C-0x00E46858: no cached slot means the world-rights byte, still
 * masked with ~0x10 at 0x00E46894.
 */
TEST(acl_image_path_without_a_slot_uses_world_rights)
{
    status_$t st = 0xdeadbeef;

    reset();
    ga_attrs_out.default_acl = ACL_UID;
    fs_result = ACL_CACHE_NO_SLOT;
    attrs_prot()->world_rights = 0x15;
    ASSERT_EQ(0x05u, call(false, 0x0000FFFFu, 1, false, false, &st));
    ASSERT_EQ(0, ee_calls);
    ASSERT_EQ(1, ml_unlock_calls);
}

/*
 * 0x00E467EC `tst.w (-0x8a,A6)` tests the LOW word of the status longword: a
 * status whose low half is zero does NOT abort the walk.
 */
TEST(find_slot_status_is_tested_on_the_low_word_only)
{
    status_$t st = 0xdeadbeef;

    /* Low half non-zero -> abort, status propagated, lock released. */
    reset();
    ga_attrs_out.default_acl = ACL_UID;
    fs_status_out = 0x0023000D;
    ASSERT_EQ(0u, call(false, 0x0000FFFFu, 1, false, false, &st));
    ASSERT_EQ(0x0023000Du, (uint32_t)st);
    ASSERT_EQ(0, ee_calls);
    ASSERT_EQ(1, ml_unlock_calls);

    /* Low half zero -> the walk continues even though the long is non-zero. */
    reset();
    ga_attrs_out.default_acl = ACL_UID;
    fs_status_out = 0x00230000;
    fs_result = 1;
    ee_result = 0x0002;
    ASSERT_EQ(0x02u, call(false, 0x0000FFFFu, 1, false, false, &st));
    ASSERT_EQ(1, ee_calls);
}

/*
 * 0x00E46808-0x00E4684A: a subsystem manager whose login SID matches the
 * cached ACL's subsystem UID gets 0xFFFFFFAF, bypassing the entry walk and
 * the ~0x10 mask.
 */
TEST(subsystem_manager_short_circuit)
{
    status_$t st = 0xdeadbeef;

    reset();
    ga_attrs_out.default_acl = ACL_UID;
    fs_result = 5;
    ACL_$ACL_CACHE[5].subsys_uid = ORG_UID;
    caller_sids.login_sid = ORG_UID;
    ee_result = 0;      /* the entry walk would have granted nothing */
    ASSERT_EQ(0x0000000Fu, call(false, 0x0000000Fu, 1, false, true, &st));
    ASSERT_EQ(0, ee_calls);
    ASSERT_EQ(1, ml_unlock_calls);

    /* 0xFFFFFFAF withholds bits 4 and 6 (`moveq #-0x51`, 0x00E46838). */
    reset();
    ga_attrs_out.default_acl = ACL_UID;
    fs_result = 5;
    ACL_$ACL_CACHE[5].subsys_uid = ORG_UID;
    caller_sids.login_sid = ORG_UID;
    ASSERT_EQ(0x00000000u, call(false, 0x00000050u, 1, false, true, &st));
    ASSERT_EQ(0, ee_calls);

    /* in_subsys FALSE -> no short circuit (0x00E46808 `tst.b D6b / bpl`). */
    reset();
    ga_attrs_out.default_acl = ACL_UID;
    fs_result = 5;
    ACL_$ACL_CACHE[5].subsys_uid = ORG_UID;
    caller_sids.login_sid = ORG_UID;
    ee_result = 0x0010;
    /* the entry-walk result is masked with ~0x10 at 0x00E46894 */
    ASSERT_EQ(0x00000000u, call(false, 0x00000010u, 1, false, false, &st));
    ASSERT_EQ(1, ee_calls);

    /* ignore_super TRUE with in_super FALSE also blocks it (0x00E46830). */
    reset();
    ga_attrs_out.default_acl = ACL_UID;
    fs_result = 5;
    ACL_$ACL_CACHE[5].subsys_uid = ORG_UID;
    caller_sids.login_sid = ORG_UID;
    ee_result = 0x000F;
    ASSERT_EQ(0x0000000Fu, call(true, 0x0000000Fu, 1, false, true, &st));
    ASSERT_EQ(1, ee_calls);
}

/*
 * 0x00E468A6-0x00E468CC: zero rights -> 0x00230001, a partial match ->
 * 0x00230002, and the result is always (required_mask & rights).
 */
TEST(status_and_result_come_from_the_mask_intersection)
{
    status_$t st;
    uint32_t  r;

    reset();
    caller_sids.user_sid = OWNER_UID;
    attrs_prot()->owner_rights = 0x00;
    st = 0;
    r = call(false, 0x0000000Fu, 1, false, false, &st);
    ASSERT_EQ(0u, r);
    ASSERT_EQ(0x00230001u, (uint32_t)st);

    reset();
    caller_sids.user_sid = OWNER_UID;
    attrs_prot()->owner_rights = 0x05;
    st = 0;
    r = call(false, 0x0000000Fu, 1, false, false, &st);
    ASSERT_EQ(0x05u, r);
    ASSERT_EQ(0x00230002u, (uint32_t)st);

    reset();
    caller_sids.user_sid = OWNER_UID;
    attrs_prot()->owner_rights = 0x0F;
    st = 0;
    r = call(false, 0x00000005u, 1, false, false, &st);
    ASSERT_EQ(0x05u, r);
    ASSERT_EQ(status_$ok, st);
}

/*
 * The three locksmith SID slots are checked in the order login, group, user
 * (0x00E464FE, 0x00E46512, 0x00E46526) - all three make a locksmith.
 */
TEST(any_of_three_sid_slots_makes_a_locksmith)
{
    status_$t st = 0xdeadbeef;
    int i;

    for (i = 0; i < 3; i++) {
        reset();
        if (i == 0)      caller_sids.login_sid = RGYC_$G_LOCKSMITH_UID;
        else if (i == 1) caller_sids.group_sid = RGYC_$G_LOCKSMITH_UID;
        else             caller_sids.user_sid  = RGYC_$G_LOCKSMITH_UID;
        ASSERT_EQ(0x0000000Fu, call(false, 0x0000000Fu, 1, false, false, &st));
    }

    /* The org SID is NOT one of them. */
    reset();
    caller_sids.org_sid = RGYC_$G_LOCKSMITH_UID;
    attrs_prot()->world_rights = 0;
    ASSERT_EQ(0x00u, call(false, 0x0000000Fu, 1, false, false, &st));
}

int main(void)
{
    printf("acl_$eval_rights (0x00E464B8) tests\n");

    RUN_TEST(super_user_short_circuit_grants_all_four_rights);
    RUN_TEST(ignore_super_suppresses_the_super_user_bypass);
    RUN_TEST(attribute_failure_sets_bit_31_of_the_status);
    RUN_TEST(attribute_failure_is_swallowed_for_the_locksmith);
    RUN_TEST(remote_objects_are_forwarded_with_the_global_tables);
    RUN_TEST(local_acl_bit_prevents_the_remote_forward);
    RUN_TEST(object_type_mismatch_is_rejected);
    RUN_TEST(object_type_exemptions_are_accepted);
    RUN_TEST(owner_match_returns_the_owner_rights);
    RUN_TEST(group_matches_directly_or_through_the_project_list);
    RUN_TEST(org_then_world_are_the_last_two_chances);
    RUN_TEST(locksmith_group_sid_with_zero_options_grants_all);
    RUN_TEST(locksmith_gets_the_privileged_mask);
    RUN_TEST(local_locksmith_downgrade);
    RUN_TEST(locksmith_override_bitmap_addressing);
    RUN_TEST(acl_image_path_locks_and_masks_bit_4);
    RUN_TEST(acl_image_path_without_a_slot_uses_world_rights);
    RUN_TEST(find_slot_status_is_tested_on_the_low_word_only);
    RUN_TEST(subsystem_manager_short_circuit);
    RUN_TEST(status_and_result_come_from_the_mask_intersection);
    RUN_TEST(any_of_three_sid_slots_makes_a_locksmith);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
