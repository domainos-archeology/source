/*
 * name/test/test_old_delete_entryu.c - Unit tests for
 * NAME_$OLD_DELETE_ENTRYU (0x00E56B08)
 *
 * name/old_delete_entryu.c is #included below and driven through mocks of
 * every routine it calls, so all assertions exercise the shipping code.
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

#include "name/name_internal.h"
#include "dir/dir.h"

uint16_t   PROC1_$AS_ID;
uint32_t   NAME_$CONST_ZERO_L;
name_$data_t NAME_$DATA;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static const uid_t DIR_UID  = { 0x11111111u, 0x22222222u };
static const uid_t OBJ_UID  = { 0x33333333u, 0x44444444u };
static const uid_t ROOT_UID = { 0x55555555u, 0x66666666u };

/* --- DIR_$OLD_GET_ENTRYU --- */
static int              ge_calls;
static dir_$old_entry_t ge_entry_out;
static status_$t        ge_status_out;
static uint16_t        *ge_name_len_ptr;

void DIR_$OLD_GET_ENTRYU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret)
{
    (void)dir_uid; (void)name;
    ge_calls++;
    ge_name_len_ptr = name_len;
    memcpy(entry_ret, &ge_entry_out, sizeof(ge_entry_out));
    *status_ret = ge_status_out;
}

/* --- ACL_$RIGHTS --- */
static int       ar_calls;
static uid_t     ar_uid[4];
static boolean   ar_ignore_super[4];
static uint32_t  ar_mask[4];
static int16_t   ar_opts[4];
static uint32_t  ar_result[4];
static status_$t ar_status[4];

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status_ret)
{
    int i = ar_calls < 3 ? ar_calls : 3;
    ar_uid[i]          = *uid;
    ar_ignore_super[i] = *ignore_super;
    ar_mask[i]         = *required_mask;
    ar_opts[i]         = *option_flags;
    ar_calls++;
    *status_ret = ar_status[i];
    return ar_result[i];
}

/* --- NAME_CONVERT_ACL_STATUS --- */
static int nc_calls;
void NAME_CONVERT_ACL_STATUS(status_$t *status_ret)
{
    nc_calls++;
    *status_ret = 0x000E0013;   /* whatever it maps to; identity for the test */
}

/* --- AST_$GET_LOCATION --- */
static int             gl_calls;
static file_$obj_loc_t gl_out;
static status_$t       gl_status_out;

void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *vol_uid_out,
                       status_$t *status)
{
    uid_t saved = loc_rec->uid;
    (void)flags; (void)unused; (void)vol_uid_out;
    gl_calls++;
    *loc_rec = gl_out;
    loc_rec->uid = saved;
    *status = gl_status_out;
}

/* --- AST_$GET_COMMON_ATTRIBUTES --- */
static int                ca_calls;
static ast_$common_attr_t ca_out;
static status_$t          ca_status_out;
static file_$obj_loc_t    ca_loc_out;
static int                ca_use_loc_out;

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    ca_calls++;
    (void)flags;
    if (ca_use_loc_out) {
        uid_t saved = loc_rec->uid;
        *loc_rec = ca_loc_out;
        loc_rec->uid = saved;
    }
    *attrs  = ca_out;
    *status = ca_status_out;
}

/* --- name_$validate_leaf --- */
static int    vl_calls;
static int8_t vl_result;
int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    (void)name;
    vl_calls++;
    *parsed_len = name_len;
    parsed[0] = 'x';
    return vl_result;
}

/* --- FILE_$PRIV_LOCK --- */
static int       pl_calls;
static uint16_t  pl_lock_mode;
static uint16_t  pl_side;
static boolean   pl_local_only;
static uint16_t  pl_flags;
static status_$t pl_status_out;
static void    **pl_acl_ctx;

void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t side,
                     uint16_t lock_mode, boolean local_only,
                     uint16_t flags, uint16_t key,
                     uint32_t rem_key, uint32_t rem_node, uint32_t rem_extra,
                     void **acl_ctx, uint16_t rem_wait,
                     uint32_t *slot_io, uint16_t *rights_out,
                     status_$t *status_ret)
{
    (void)file_uid; (void)asid; (void)key; (void)rem_key; (void)rem_node;
    (void)rem_extra; (void)rem_wait;
    pl_calls++;
    pl_side       = side;
    pl_lock_mode  = lock_mode;
    pl_local_only = local_only;
    pl_flags      = flags;
    pl_acl_ctx    = acl_ctx;
    *slot_io   = 0x1234;
    *rights_out = 0;
    *status_ret = pl_status_out;
}

/* --- FILE_$PRIV_UNLOCK --- */
static int      pu_calls;
static uint16_t pu_lock_mode;
static int32_t  pu_slot;
static status_$t pu_status_out;

boolean FILE_$PRIV_UNLOCK(uid_t *file_uid, int32_t lock_slot,
                          uint16_t lock_mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv_out, status_$t *status_ret)
{
    (void)file_uid; (void)asid; (void)by_key; (void)key; (void)rem_key;
    (void)rem_node; (void)dtv_out;
    pu_calls++;
    pu_slot      = lock_slot;
    pu_lock_mode = lock_mode;
    *status_ret  = pu_status_out;
    return 0;
}

/* --- REM_FILE_$DROP_HARD_LINKU --- */
static int       dh_calls;
static uint16_t  dh_flags[3];
static uint16_t  dh_name_len;
static status_$t dh_status_out[3];

void REM_FILE_$DROP_HARD_LINKU(void *addr_info, uid_t *dir_uid,
                               char *name, uint16_t name_len,
                               uint16_t flags, status_$t *status)
{
    int i = dh_calls < 2 ? dh_calls : 2;
    (void)addr_info; (void)dir_uid; (void)name;
    dh_flags[i]  = flags;
    dh_name_len  = name_len;
    dh_calls++;
    *status = dh_status_out[i];
}

/* --- FILE_$READ_LOCK_ENTRYUI --- */
static int       rl_calls;
static status_$t rl_status_out;
void FILE_$READ_LOCK_ENTRYUI(uid_t *file_uid, void *info_out,
                             status_$t *status_ret)
{
    (void)file_uid; (void)info_out;
    rl_calls++;
    *status_ret = rl_status_out;
}

/* --- FILE_$DELETE_OBJ --- */
static int       do_calls;
static int8_t    do_force;
static status_$t do_status_out;
void FILE_$DELETE_OBJ(uid_t *file_uid, int8_t force, void *param_3,
                      status_$t *status_ret)
{
    (void)file_uid;
    do_calls++;
    do_force = force;
    *(int8_t *)param_3 = 0;
    *status_ret = do_status_out;
}

/* --- name_$old_drop_entry --- */
static int       de_calls;
static uint16_t  de_type;
static uint16_t  de_name_len;
static status_$t de_status_out;
void name_$old_drop_entry(uid_t *dir_uid, char *name, uint16_t name_len,
                          uint16_t type, void *result, status_$t *status_ret)
{
    (void)dir_uid; (void)name; (void)result;
    de_calls++;
    de_type     = type;
    de_name_len = name_len;
    *status_ret = de_status_out;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

#include "../old_delete_entryu.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static uint8_t result_buf[8];

static void reset(void)
{
    memset(&ge_entry_out, 0, sizeof(ge_entry_out));
    memset(&gl_out, 0, sizeof(gl_out));
    memset(&ca_out, 0, sizeof(ca_out));
    memset(&ca_loc_out, 0, sizeof(ca_loc_out));
    memset(result_buf, 0, sizeof(result_buf));
    memset(ar_result, 0, sizeof(ar_result));
    memset(ar_status, 0, sizeof(ar_status));
    memset(dh_status_out, 0, sizeof(dh_status_out));

    ge_calls = ar_calls = nc_calls = gl_calls = ca_calls = vl_calls = 0;
    pl_calls = pu_calls = dh_calls = rl_calls = do_calls = de_calls = 0;
    ge_status_out = gl_status_out = ca_status_out = status_$ok;
    pl_status_out = pu_status_out = rl_status_out = status_$ok;
    do_status_out = de_status_out = status_$ok;
    vl_result = -1;                 /* valid leaf */
    PROC1_$AS_ID = 3;
    NAME_$ROOT_UID = ROOT_UID;

    /* The default shape: a plain local file that both records agree on.
     * AST_$GET_COMMON_ATTRIBUTES overwrites the whole 0x20-byte location
     * record on every non-error path (ast/ast.h), so the mock always does. */
    ge_entry_out.type = NAME_OLD_ENTRY_FILE;
    ge_entry_out.uid  = OBJ_UID;
    gl_out.volume = 7;
    gl_out.node   = 0x0ABCDEF0u;
    gl_out.flags  = 0;
    ca_use_loc_out = 1;
    ca_loc_out.volume = 7;
    ca_loc_out.node   = 0x0ABCDEF0u;
    ca_loc_out.flags  = 0;
    ca_out.sub_type = 0;
    ca_out.refcount = 5;
    /* bit 3 (delete) set, bit 6 (protected) clear */
    ar_result[0] = 0x0000000Fu;
    ar_result[1] = 0x0000000Fu;
}

static status_$t call(boolean check_del, boolean no_lock, boolean allow_link)
{
    uid_t dir = DIR_UID;
    status_$t st = 0xdeadbeef;
    NAME_$OLD_DELETE_ENTRYU(&dir, "abc", 3, check_del, no_lock, allow_link,
                            result_buf, &st);
    return st;
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/*
 * 0x00E56B2E `pea (0x10,A6)`: the name length is passed BY ADDRESS to
 * DIR_$OLD_GET_ENTRYU, and a failure there returns immediately (0x00E56B3E).
 */
TEST(entry_lookup_takes_the_length_by_address_and_can_fail)
{
    reset();
    ge_status_out = 0x000E0007;
    ASSERT_EQ(0x000E0007u, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, ge_calls);
    ASSERT_TRUE(ge_name_len_ptr != NULL);
    ASSERT_EQ(0, ar_calls);
}

/*
 * 0x00E56B44-0x00E56B68: type 1 proceeds, type 3 only when allow_link is
 * TRUE, anything else is "invalid leaf".
 */
TEST(entry_type_gate)
{
    reset();
    ge_entry_out.type = NAME_OLD_ENTRY_FILE;
    ASSERT_EQ(status_$ok, call(false, false, false));

    reset();
    ge_entry_out.type = NAME_OLD_ENTRY_LINK;
    ASSERT_EQ(0x000E000Au, (uint32_t)call(false, false, false));

    reset();
    ge_entry_out.type = NAME_OLD_ENTRY_LINK;
    ASSERT_EQ(status_$ok, call(false, false, true));

    reset();
    ge_entry_out.type = 5;
    ASSERT_EQ(0x000E000Bu, (uint32_t)call(false, false, false));
}

/*
 * 0x00E56B6C-0x00E56B88: the directory check uses option flags 1 and mask
 * 0x03 with ignore_super FALSE; a failure is translated by
 * NAME_CONVERT_ACL_STATUS (0x00E56CBE).
 */
TEST(directory_rights_check_constants)
{
    reset();
    (void)call(false, false, false);

    ASSERT_TRUE(ar_calls >= 1);
    ASSERT_EQ(DIR_UID.high, ar_uid[0].high);
    ASSERT_EQ(0x00, (unsigned char)ar_ignore_super[0]);
    ASSERT_EQ(0x00000003u, ar_mask[0]);
    ASSERT_EQ(1, ar_opts[0]);

    reset();
    ar_status[0] = 0x00230001;
    (void)call(false, false, false);
    ASSERT_EQ(1, nc_calls);
    ASSERT_EQ(0, gl_calls);
}

/*
 * 0x00E56C6C-0x00E56C84: the object check uses option flags 0xFFFF and mask
 * 0x48, and it is made on the OBJECT's UID from the directory entry.
 */
TEST(object_rights_check_constants)
{
    reset();
    (void)call(false, false, false);

    ASSERT_EQ(2, ar_calls);
    ASSERT_EQ(OBJ_UID.high, ar_uid[1].high);
    ASSERT_EQ(OBJ_UID.low,  ar_uid[1].low);
    ASSERT_EQ(0x00, (unsigned char)ar_ignore_super[1]);
    ASSERT_EQ(0x00000048u, ar_mask[1]);
    ASSERT_EQ((uint16_t)0xFFFF, (uint16_t)ar_opts[1]);
}

/*
 * 0x00E56C26-0x00E56C3E: the object must agree with the directory on both
 * the volume word and the node longword, or the entry is simply unlinked
 * without touching any object.
 */
TEST(location_mismatch_only_unlinks)
{
    reset();
    ca_loc_out.volume = 9;              /* directory says 7 */
    ASSERT_EQ(status_$ok, call(false, false, false));
    ASSERT_EQ(1, de_calls);             /* the entry was dropped */
    ASSERT_EQ(0, do_calls);             /* but no object was deleted */
    ASSERT_EQ(1, ar_calls);             /* and no object rights check */

    reset();
    ca_loc_out.node   = 0x0BADF00Du;
    ASSERT_EQ(status_$ok, call(false, false, false));
    ASSERT_EQ(1, de_calls);
    ASSERT_EQ(0, do_calls);
}

/*
 * 0x00E56C44-0x00E56C50: a location failure other than "object not found"
 * aborts; "object not found" degrades to the unlink-only path.
 */
TEST(attribute_failure_handling)
{
    reset();
    ca_status_out = 0x00030001;
    ASSERT_EQ(0x00030001u, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, de_calls);

    reset();
    ca_status_out = 0x000F0001;         /* file_$object_not_found */
    ASSERT_EQ(status_$ok, call(false, false, false));
    ASSERT_EQ(1, de_calls);
    ASSERT_EQ(0, do_calls);
}

/*
 * 0x00E56C5A-0x00E56C68: a non-zero sub-type is "name is not a file" unless
 * the caller allows links.
 */
TEST(subtype_gate)
{
    reset();
    ca_out.sub_type = 2;
    ca_out.refcount = 5;
    ASSERT_EQ(0x000E0010u, (uint32_t)call(false, false, false));

    /* allow_link TRUE lets a directory through the sub-type gate; it is then
     * stopped by the reference-count gate at 0x00E56CC8 only when refcount
     * is <= 1. */
    reset();
    ca_out.sub_type = 2;
    ca_out.refcount = 5;
    ASSERT_EQ(status_$ok, call(false, false, true));
}

/*
 * 0x00E56C9E-0x00E56CB8: `not.b` on check_del_right OR'ed with "bit 3 of the
 * rights word is clear"; when that holds, bit 6 refuses the operation.
 */
TEST(delete_right_arithmetic)
{
    /* check_del_right FALSE + bit 6 set -> refused */
    reset();
    ar_result[1] = 0x00000048u;
    ASSERT_EQ(0x000E0014u, (uint32_t)call(false, false, false));

    /* check_del_right TRUE and bit 3 set -> the bit-6 test is skipped */
    reset();
    ar_result[1] = 0x00000048u;
    ASSERT_EQ(status_$ok, call(true, false, false));

    /* check_del_right TRUE but bit 3 clear -> bit 6 still refuses */
    reset();
    ar_result[1] = 0x00000040u;
    ASSERT_EQ(0x000E0014u, (uint32_t)call(true, false, false));

    /* bit 6 clear -> always allowed */
    reset();
    ar_result[1] = 0x00000000u;
    ASSERT_EQ(status_$ok, call(false, false, false));
}

/*
 * 0x00E56C88-0x00E56C9C: only ok / 0x230001 / 0x230002 continue; any other
 * ACL status goes through NAME_CONVERT_ACL_STATUS.
 */
TEST(object_acl_status_triage)
{
    reset();
    ar_status[1] = 0x00230002;
    ASSERT_EQ(status_$ok, call(false, false, false));
    ASSERT_EQ(0, nc_calls);

    reset();
    ar_status[1] = 0x00230004;
    (void)call(false, false, false);
    ASSERT_EQ(1, nc_calls);
    ASSERT_EQ(0, de_calls);
}

/*
 * 0x00E56CC8-0x00E56CE0: `cmp.w (-0x2c,A6),D6w` with D6 = 1 and `bcs` is an
 * unsigned "1 < refcount", so refcount <= 1 on a sub-type 1 or 2 refuses.
 */
TEST(directory_refcount_gate)
{
    reset();
    ca_out.sub_type = 2;
    ca_out.refcount = 1;
    ASSERT_EQ(0x000E0013u, (uint32_t)call(false, false, true));

    reset();
    ca_out.sub_type = 1;
    ca_out.refcount = 0;
    ASSERT_EQ(0x000E0013u, (uint32_t)call(false, false, true));

    reset();
    ca_out.sub_type = 2;
    ca_out.refcount = 2;
    ASSERT_EQ(status_$ok, call(false, false, true));

    /* A plain file with refcount 1 is NOT stopped - the gate only applies to
     * sub-types 1 and 2. */
    reset();
    ca_out.sub_type = 0;
    ca_out.refcount = 1;
    ASSERT_EQ(status_$ok, call(false, false, false));
}

/*
 * 0x00E56DFA-0x00E56E0E: a local object is deleted with FILE_$DELETE_OBJ,
 * whose `force` argument is the no_lock boolean (`move.b D4b,-(SP)` at
 * 0x00E56E02), and the entry is then unlinked.
 */
TEST(local_delete_then_unlink)
{
    reset();
    ASSERT_EQ(status_$ok, call(false, true, false));
    ASSERT_EQ(1, do_calls);
    ASSERT_EQ(0xFF, (unsigned char)do_force);
    ASSERT_EQ(1, de_calls);
    ASSERT_EQ(0, de_type);              /* 0x00E56E1C `clr.w -(SP)` */
    ASSERT_EQ(3, de_name_len);
    ASSERT_EQ(0, dh_calls);

    /* A failed delete stops before the unlink (0x00E56E12). */
    reset();
    do_status_out = 0x00030001;
    ASSERT_EQ(0x00030001u, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, de_calls);
}

/*
 * 0x00E56CE4 `tst.b (-0x8b,A6)` + `bpl`: only an object whose location record
 * has bit 7 set takes the remote path.
 */
TEST(remote_path_locks_drops_and_unlocks)
{
    reset();
    ca_loc_out.flags  = (int8_t)FILE_OBJ_LOC_REMOTE;

    ASSERT_EQ(status_$ok, call(false, false, false));
    ASSERT_EQ(1, vl_calls);
    ASSERT_EQ(1, pl_calls);
    ASSERT_EQ(0, pl_side);              /* 0x00E56D36 `clr.w` */
    ASSERT_EQ(4, pl_lock_mode);         /* 0x00E56D30 `move.l #0x40000` */
    ASSERT_EQ(0x00, (unsigned char)pl_local_only);
    ASSERT_EQ(0, pl_flags);
    ASSERT_TRUE(pl_acl_ctx == (void **)&NAME_$CONST_ZERO_L);
    ASSERT_EQ(1, dh_calls);
    ASSERT_EQ(0, dh_flags[0]);          /* check_del_right FALSE -> 0 */
    ASSERT_EQ(3, dh_name_len);
    ASSERT_EQ(1, pu_calls);
    ASSERT_EQ(4, pu_lock_mode);
    ASSERT_EQ(0x1234, pu_slot);
    ASSERT_EQ(0, do_calls);
    ASSERT_EQ(0, de_calls);             /* the remote path never unlinks */
}

/*
 * 0x00E56D52-0x00E56D5E: the drop flag REM_FILE_$DROP_HARD_LINKU receives is
 * 1 when check_del_right is TRUE and 0 otherwise.
 */
TEST(remote_drop_flag_follows_check_del_right)
{
    reset();
    ca_loc_out.flags  = (int8_t)FILE_OBJ_LOC_REMOTE;
    ar_result[1] = 0x00000008u;         /* bit 3 set so check_del passes */

    (void)call(true, false, false);
    ASSERT_EQ(1, dh_calls);
    ASSERT_EQ(1, dh_flags[0]);
}

/*
 * 0x00E56D12 / 0x00E56DC2 `tst.b D4b` + `bmi`: no_lock TRUE skips both the
 * lock and the unlock.
 */
TEST(no_lock_skips_the_lock_pair)
{
    reset();
    ca_loc_out.flags  = (int8_t)FILE_OBJ_LOC_REMOTE;

    (void)call(false, true, false);
    ASSERT_EQ(0, pl_calls);
    ASSERT_EQ(0, pu_calls);
    ASSERT_EQ(1, dh_calls);
}

/*
 * 0x00E56D7E-0x00E56DBE: a comms failure is retried exactly once, and only
 * when FILE_$READ_LOCK_ENTRYUI reports "not locked by this process".
 */
TEST(remote_drop_retry)
{
    reset();
    ca_loc_out.flags  = (int8_t)FILE_OBJ_LOC_REMOTE;
    dh_status_out[0] = 0x000F0004;      /* comms failure */
    rl_status_out    = 0x000F0005;      /* not locked -> retry */

    (void)call(false, true, false);
    ASSERT_EQ(1, rl_calls);
    ASSERT_EQ(2, dh_calls);

    /* A different lock status suppresses the retry. */
    reset();
    ca_loc_out.flags  = (int8_t)FILE_OBJ_LOC_REMOTE;
    dh_status_out[0] = 0x000F0004;
    rl_status_out    = status_$ok;

    (void)call(false, true, false);
    ASSERT_EQ(1, rl_calls);
    ASSERT_EQ(1, dh_calls);
}

/*
 * 0x00E56CEC-0x00E56D0E: an invalid leaf name stops the remote path.
 */
TEST(remote_path_validates_the_leaf)
{
    reset();
    ca_loc_out.flags  = (int8_t)FILE_OBJ_LOC_REMOTE;
    vl_result = 0;                      /* invalid */

    ASSERT_EQ(0x000E000Bu, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, dh_calls);
}

/*
 * 0x00E56DF0-0x00E56DF8: on the remote path the caller ends up with the
 * UNLOCK's status when the drop itself succeeded.
 */
TEST(remote_path_reports_the_unlock_status)
{
    reset();
    ca_loc_out.flags  = (int8_t)FILE_OBJ_LOC_REMOTE;
    pu_status_out = 0x000F0009;

    ASSERT_EQ(0x000F0009u, (uint32_t)call(false, false, false));
}

/*
 * 0x00E56C0E-0x00E56C24: a sub-type 2 object under the ROOT directory, with
 * allow_link TRUE, is treated as "not at this location" - it is unlinked
 * rather than deleted.
 */
TEST(root_directory_entry_is_only_unlinked)
{
    reset();
    ca_out.sub_type = 2;
    ca_out.refcount = 5;
    {
        uid_t dir = ROOT_UID;
        status_$t st = 0xdeadbeef;
        NAME_$OLD_DELETE_ENTRYU(&dir, "abc", 3, false, false, true,
                                result_buf, &st);
        ASSERT_EQ(status_$ok, (uint32_t)st);
    }
    ASSERT_EQ(1, de_calls);
    ASSERT_EQ(0, do_calls);
    ASSERT_EQ(1, ar_calls);

    /* allow_link FALSE leaves the object alone -> the sub-type gate fires. */
    reset();
    ca_out.sub_type = 2;
    ca_out.refcount = 5;
    {
        uid_t dir = ROOT_UID;
        status_$t st = 0xdeadbeef;
        NAME_$OLD_DELETE_ENTRYU(&dir, "abc", 3, false, false, false,
                                result_buf, &st);
        ASSERT_EQ(0x000E0010u, (uint32_t)st);
    }
}

int main(void)
{
    printf("NAME_$OLD_DELETE_ENTRYU (0x00E56B08) tests\n");

    RUN_TEST(entry_lookup_takes_the_length_by_address_and_can_fail);
    RUN_TEST(entry_type_gate);
    RUN_TEST(directory_rights_check_constants);
    RUN_TEST(object_rights_check_constants);
    RUN_TEST(location_mismatch_only_unlinks);
    RUN_TEST(attribute_failure_handling);
    RUN_TEST(subtype_gate);
    RUN_TEST(delete_right_arithmetic);
    RUN_TEST(object_acl_status_triage);
    RUN_TEST(directory_refcount_gate);
    RUN_TEST(local_delete_then_unlink);
    RUN_TEST(remote_path_locks_drops_and_unlocks);
    RUN_TEST(remote_drop_flag_follows_check_del_right);
    RUN_TEST(no_lock_skips_the_lock_pair);
    RUN_TEST(remote_drop_retry);
    RUN_TEST(remote_path_validates_the_leaf);
    RUN_TEST(remote_path_reports_the_unlock_status);
    RUN_TEST(root_directory_entry_is_only_unlinked);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
