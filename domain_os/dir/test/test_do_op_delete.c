/*
 * dir/test/test_do_op_delete.c - Unit tests for dir_$do_op_delete
 * (0x00E5125E)
 *
 * dir/do_op_delete.c is #included below and driven through mocks of every
 * routine it calls.  The behaviours covered include the ACL_$RIGHTS call at
 * 0x00E51462 that bead source-6dil reported missing, together with its three
 * pea-cell constants.
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

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];
uid_t    UID_$NIL = { 0, 0 };

/* The dir module data area DIR_$DO_OP establishes as A5 (0xE7DC00).  The host
 * arch.h stub returns NULL, so the .c under test is compiled against this
 * buffer instead - the #define comes after every header include. */
static uint8_t a5_area[0x2200];

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static const uid_t DIR_UID   = { 0x11111111u, 0x22222222u };
static const uid_t ENTRY_UID = { 0x33333333u, 0x44444444u };

/* A fake directory handle whose +0x3A word is the volume. */
static uint8_t handle_obj[0x40];
#define HANDLE_VOLUME (*(int16_t *)(handle_obj + DIR_HANDLE_VOLUME_OFF))

static int enter_super_calls, exit_super_calls;
void ACL_$ENTER_SUPER(void) { enter_super_calls++; }
void ACL_$EXIT_SUPER(void)  { exit_super_calls++; }

/* --- dir_$open_dir --- */
static int       od_calls;
static int16_t   od_mode, od_rights;
static status_$t od_status_out;
void dir_$open_dir(void *uid, int16_t mode, int16_t rights,
                   void *handle_ret, status_$t *status_ret)
{
    (void)uid;
    od_calls++;
    od_mode = mode;
    od_rights = rights;
    *(void **)handle_ret = handle_obj;
    *status_ret = od_status_out;
}

/* --- dir_$release_handle --- */
static int rh_calls;
void dir_$release_handle(void *handle_ptr) { (void)handle_ptr; rh_calls++; }

/* --- dir_$find_entry --- */
static int     fe_calls;
static char    fe_result;
static uint8_t fe_entry[0x20];
char dir_$find_entry(void *handle, void *name, int16_t name_len,
                     int16_t flags, void **entry_ret,
                     void *extra, int16_t *depth_ret)
{
    (void)handle; (void)name; (void)name_len; (void)flags;
    (void)extra; (void)depth_ret;
    fe_calls++;
    *entry_ret = fe_entry;
    return fe_result;
}

/* --- dir_$remove_entry --- */
static int       re_calls;
static uid_t    *re_uid_ret[3];
static status_$t re_status_out;
void dir_$remove_entry(void *handle, void *name, int16_t name_len,
                       int16_t op_type, void *uid_ret, status_$t *status_ret)
{
    int i = re_calls < 2 ? re_calls : 2;
    (void)handle; (void)name; (void)name_len; (void)op_type;
    re_uid_ret[i] = (uid_t *)uid_ret;
    re_calls++;
    *status_ret = re_status_out;
}

/* --- AST_$GET_COMMON_ATTRIBUTES --- */
static int                ca_calls;
static ast_$common_attr_t ca_out;
static status_$t          ca_status_out;
static uint16_t           ca_flags_seen;
static file_$obj_loc_t    ca_loc_out;
void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    uid_t saved = loc_rec->uid;
    ca_calls++;
    ca_flags_seen = flags;
    *loc_rec = ca_loc_out;
    loc_rec->uid = saved;
    *attrs = ca_out;
    *status = ca_status_out;
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

static int nc_calls;
void NAME_CONVERT_ACL_STATUS(status_$t *status_ret)
{
    nc_calls++;
    *status_ret = 0x000E0013;
}

/* --- AST_$SET_ATTRIBUTE --- */
static int       sa_calls;
static uint16_t  sa_attr;
static uint16_t  sa_value;
static status_$t sa_status_out;
void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status)
{
    (void)uid;
    sa_calls++;
    sa_attr  = attr_id;
    sa_value = *(uint16_t *)value;
    *status  = sa_status_out;
}

/* --- FILE_$PRIV_LOCK --- */
static int       pl_calls;
static uint16_t  pl_side, pl_mode, pl_flags;
static boolean   pl_local_only;
static status_$t pl_status_out;
void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t side,
                     uint16_t lock_mode, boolean local_only,
                     uint16_t flags, uint16_t key,
                     uint32_t rem_key, uint32_t rem_node, uint32_t rem_extra,
                     void **acl_ctx, uint16_t rem_wait,
                     uint32_t *slot_io, uint16_t *rights_out,
                     status_$t *status_ret)
{
    (void)file_uid; (void)asid; (void)key; (void)rem_key; (void)rem_node;
    (void)rem_extra; (void)acl_ctx; (void)rem_wait;
    pl_calls++;
    pl_side = side; pl_mode = lock_mode; pl_flags = flags;
    pl_local_only = local_only;
    *slot_io = 0x99;
    *rights_out = 0;
    *status_ret = pl_status_out;
}

/* --- FILE_$UNLOCK_D --- */
static int      ud_calls;
static uint16_t ud_mode;
static uint32_t ud_slot;
void FILE_$UNLOCK_D(uid_t *file_uid, uint32_t *lock_index, uint16_t *lock_mode,
                    status_$t *status_ret)
{
    (void)file_uid;
    ud_calls++;
    ud_slot = *lock_index;
    ud_mode = *lock_mode;
    *status_ret = status_$ok;
}

/* --- FILE_$DELETE_OBJ --- */
static int       do_calls;
static int8_t    do_force;
static int8_t    do_flag_out;
static status_$t do_status_out;
void FILE_$DELETE_OBJ(uid_t *file_uid, int8_t force, void *param_3,
                      status_$t *status_ret)
{
    (void)file_uid;
    do_calls++;
    do_force = force;
    *(int8_t *)param_3 = do_flag_out;
    *status_ret = do_status_out;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

/* Point the whole DIR module block at our own buffer (source-yv13). */
#undef DIR_$BLOCK_BASE
#define DIR_$BLOCK_BASE ((void *)a5_area)

#include "../do_op_delete.c"

/* ------------------------------------------------------------------ */

#define TEST_PID 4

static uid_t entry_uid_ret;
static uid_t deleted_uid_ret;

static void reset(void)
{
    memset(a5_area, 0, sizeof(a5_area));
    memset(handle_obj, 0, sizeof(handle_obj));
    memset(fe_entry, 0, sizeof(fe_entry));
    memset(&ca_out, 0, sizeof(ca_out));
    memset(&ca_loc_out, 0, sizeof(ca_loc_out));
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));

    enter_super_calls = exit_super_calls = 0;
    od_calls = rh_calls = fe_calls = re_calls = ca_calls = 0;
    ar_calls = nc_calls = sa_calls = pl_calls = ud_calls = do_calls = 0;
    od_status_out = re_status_out = ca_status_out = status_$ok;
    ar_status_out = sa_status_out = pl_status_out = do_status_out = status_$ok;
    ar_result = 0;
    do_flag_out = -1;
    PROC1_$CURRENT = TEST_PID;
    PROC1_$AS_ID   = 6;

    /* Default: the entry is found, is a plain file, on this handle's volume,
     * and the caller has every right that matters. */
    fe_result = -1;                             /* found */
    fe_entry[0] = 1;                            /* entry type 1 */
    *(uid_t *)(fe_entry + 4) = ENTRY_UID;
    HANDLE_VOLUME = 3;
    ca_loc_out.volume = 3;
    ca_out.sub_type = 0;
    ca_out.refcount = 5;
    ca_out.access_flags = 0;
    entry_uid_ret.high = entry_uid_ret.low = 0xEEEEEEEEu;
    deleted_uid_ret.high = deleted_uid_ret.low = 0xEEEEEEEEu;
}

static status_$t call(boolean check_del, boolean entry_only, boolean allow_link)
{
    uid_t dir = DIR_UID;
    status_$t st = 0xdeadbeef;
    dir_$do_op_delete(&dir, "n", 1, check_del, entry_only, allow_link,
                      &entry_uid_ret, &deleted_uid_ret, &st);
    return st;
}

/* ------------------------------------------------------------------ */

/*
 * 0x00E5127A-0x00E512AE: the deleted-object cell is cleared to UID_$NIL and
 * the directory is opened inside an ACL_$ENTER_SUPER / ACL_$EXIT_SUPER pair
 * with the merged mode/rights longword 0x00020003.
 */
TEST(prologue_clears_the_uid_and_opens_the_directory_as_super)
{
    reset();
    od_status_out = 0x000E000D;
    ASSERT_EQ(0x000E000Du, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, enter_super_calls);
    ASSERT_EQ(1, exit_super_calls);
    ASSERT_EQ(2, od_mode);
    ASSERT_EQ(3, od_rights);
    ASSERT_EQ(0u, deleted_uid_ret.high);
    ASSERT_EQ(0u, deleted_uid_ret.low);
    ASSERT_EQ(1, rh_calls);         /* the handle is always released */
    ASSERT_EQ(0, fe_calls);
}

/*
 * 0x00E512E0-0x00E51302: a missing entry is "name not found" - except for a
 * type-9 process, which gets silence.
 */
TEST(missing_entry)
{
    reset();
    fe_result = 0;                  /* not found */
    ASSERT_EQ(0x000E0007u, (uint32_t)call(false, false, false));

    reset();
    fe_result = 0;
    PROC1_$TYPE[TEST_PID] = 9;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
}

/*
 * 0x00E51306-0x00E51338: a mount entry (type 4) is either unlinked outright
 * or refused with "invalid link operation".
 */
TEST(mount_entry)
{
    reset();
    fe_entry[0] = DIR_ENTRY_TYPE_MOUNT;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, true));
    ASSERT_EQ(1, re_calls);
    ASSERT_TRUE(re_uid_ret[0] == &entry_uid_ret);
    ASSERT_EQ(0, ca_calls);

    reset();
    fe_entry[0] = DIR_ENTRY_TYPE_MOUNT;
    ASSERT_EQ(0x000E000Au, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, re_calls);
}

/* 0x00E5133C: the entry's UID always reaches the caller. */
TEST(entry_uid_is_returned)
{
    reset();
    (void)call(false, false, false);
    ASSERT_EQ(ENTRY_UID.high, entry_uid_ret.high);
    ASSERT_EQ(ENTRY_UID.low,  entry_uid_ret.low);
}

/*
 * 0x00E51346-0x00E5134E: dropping a soft link with allow_link set never looks
 * at an object at all.
 */
TEST(soft_link_drop_skips_the_object)
{
    reset();
    fe_entry[0] = DIR_ENTRY_TYPE_SOFT_LINK;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, true));
    ASSERT_EQ(0, ca_calls);
    ASSERT_EQ(1, re_calls);
    ASSERT_EQ(0, do_calls);
}

/*
 * 0x00E5137E-0x00E51390: the attributes are used only when the fetch worked
 * AND the object's volume matches the handle's (handle+0x3A).
 */
TEST(volume_mismatch_falls_back_to_unlink_only)
{
    reset();
    ca_loc_out.volume = 9;          /* the handle says 3 */
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, ca_calls);
    ASSERT_EQ(0, ar_calls);
    ASSERT_EQ(1, re_calls);
    ASSERT_EQ(0, do_calls);         /* drop_object was cleared */

    /* file_$object_not_found is also tolerated. */
    reset();
    ca_status_out = 0x000F0001;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, re_calls);
    ASSERT_EQ(0, do_calls);

    /* any other failure aborts */
    reset();
    ca_status_out = 0x00030001;
    ASSERT_EQ(0x00030001u, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, re_calls);
}

/* 0x00E5136C `move.w #0x81`. */
TEST(common_attributes_flag_word)
{
    reset();
    (void)call(false, false, false);
    ASSERT_EQ(0x0081, ca_flags_seen);
}

/*
 * 0x00E513B2-0x00E513EA: the two access_flags gates - bit 6 with a reference
 * count below two, and bit 7 for a type-9 process.
 */
TEST(access_flag_gates)
{
    reset();
    ca_out.access_flags = (int8_t)DIR_CATTR_ACCESS_REFCOUNTED;
    ca_out.refcount = 1;
    ASSERT_EQ(0x000F0010u, (uint32_t)call(false, false, false));

    reset();
    ca_out.access_flags = (int8_t)DIR_CATTR_ACCESS_REFCOUNTED;
    ca_out.refcount = 2;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));

    reset();
    ca_out.access_flags = (int8_t)0x80;
    PROC1_$TYPE[TEST_PID] = 9;
    ASSERT_EQ(0x0003000Au, (uint32_t)call(false, false, false));

    /* bit 7 alone, non-type-9, is fine */
    reset();
    ca_out.access_flags = (int8_t)0x80;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
}

/* 0x00E513EE-0x00E51412: sub-type 5, 4 or 0, or allow_link. */
TEST(sub_type_gate)
{
    int16_t ok[] = { 0, 4, 5 };
    unsigned i;
    for (i = 0; i < 3; i++) {
        reset();
        ca_out.sub_type = (uint8_t)ok[i];
        ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
    }

    reset();
    ca_out.sub_type = 3;
    ASSERT_EQ(0x000E0010u, (uint32_t)call(false, false, false));

    reset();
    ca_out.sub_type = 3;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, true));
}

/*
 * 0x00E51416-0x00E5144C: a directory that is the source of a mount cannot be
 * deleted.  `move.w (0x155a,A5),D0w` reads the LOW half of the longword
 * count at A5+0x1558 (see DIR_MOUNT_COUNT16 in dir/dir_internal.h), so the
 * count is planted as that longword; the rows start at A5+8+0x1554.
 */
TEST(mount_source_directory_is_refused)
{
    reset();
    ca_out.sub_type = 2;
    *(int32_t *)(a5_area + DIR_MOUNT_COUNT_OFF) = 3;
    {
        uint32_t *row = (uint32_t *)(a5_area + 8 + 0x1554 + 2 * 8);
        row[0] = ENTRY_UID.high;
        row[1] = ENTRY_UID.low;
    }
    ASSERT_EQ(0x000E0016u, (uint32_t)call(false, false, true));
    ASSERT_EQ(0, ar_calls);

    /* A row that does not match lets the delete proceed. */
    reset();
    ca_out.sub_type = 2;
    *(int32_t *)(a5_area + DIR_MOUNT_COUNT_OFF) = 3;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, true));
    ASSERT_EQ(1, ar_calls);

    /* A non-directory never consults the table. */
    reset();
    ca_out.sub_type = 0;
    *(int32_t *)(a5_area + DIR_MOUNT_COUNT_OFF) = 3;
    {
        uint32_t *row = (uint32_t *)(a5_area + 8 + 0x1554 + 8);
        row[0] = ENTRY_UID.high;
        row[1] = ENTRY_UID.low;
    }
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
}

/*
 * source-6dil: the call at 0x00E51462, on the OBJECT's UID, with
 * ignore_super = 0x00E4CFF4 (FALSE), mask = 0x00E505C6 (0x48) and option
 * flags = 0x00E505C4 (0xFFFF).
 */
TEST(object_rights_check_uses_the_recovered_constants)
{
    reset();
    (void)call(false, false, false);

    ASSERT_EQ(1, ar_calls);
    ASSERT_EQ(ENTRY_UID.high, ar_uid.high);
    ASSERT_EQ(ENTRY_UID.low,  ar_uid.low);
    ASSERT_EQ(0x00, (unsigned char)ar_ignore_super);
    ASSERT_EQ(0x00000048u, ar_mask);
    ASSERT_EQ((uint16_t)0xFFFF, (uint16_t)ar_opts);
}

/* 0x00E5146C-0x00E514AC: only ok/0x230001/0x230002 continue. */
TEST(acl_status_triage)
{
    reset();
    ar_status_out = 0x00230002;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, nc_calls);

    reset();
    ar_status_out = 0x00230004;
    ASSERT_EQ(0x000E0013u, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, nc_calls);
    ASSERT_EQ(0, re_calls);
}

/* 0x00E51482-0x00E5149C: the delete-right / protected-bit arithmetic. */
TEST(delete_right_arithmetic)
{
    reset();
    ar_result = 0x48;
    ASSERT_EQ(0x000E0014u, (uint32_t)call(false, false, false));

    reset();
    ar_result = 0x48;
    ASSERT_EQ(status_$ok, (uint32_t)call(true, false, false));

    reset();
    ar_result = 0x40;
    ASSERT_EQ(0x000E0014u, (uint32_t)call(true, false, false));

    reset();
    ar_result = 0x00;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
}

/*
 * 0x00E514AE-0x00E514F0: a directory is dereferenced with AST_$SET_ATTRIBUTE
 * selector 7, and 0x00030007 becomes the naming "no rights".
 */
TEST(directory_is_dereferenced_not_deleted)
{
    reset();
    ca_out.sub_type = 2;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, true));
    ASSERT_EQ(1, sa_calls);
    ASSERT_EQ(7, sa_attr);
    ASSERT_EQ(1, sa_value);
    ASSERT_EQ(0, pl_calls);
    ASSERT_EQ(1, re_calls);
    ASSERT_EQ(0, do_calls);         /* drop_object was cleared */

    reset();
    ca_out.sub_type = 1;
    sa_status_out = 0x00030007;
    ASSERT_EQ(0x000E0013u, (uint32_t)call(false, false, true));
    ASSERT_EQ(0, re_calls);
}

/*
 * 0x00E514F2-0x00E5152E: a plain object is locked first, with side 0 and mode
 * 4 (the merged `pea (0x4).w`), local_only TRUE, flags 0.
 */
TEST(object_is_locked_before_the_unlink)
{
    reset();
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, pl_calls);
    ASSERT_EQ(0, pl_side);
    ASSERT_EQ(4, pl_mode);
    ASSERT_EQ(0xFF, (unsigned char)pl_local_only);
    ASSERT_EQ(0, pl_flags);
    ASSERT_EQ(1, ud_calls);
    ASSERT_EQ(4, ud_mode);
    ASSERT_EQ(0x99u, ud_slot);

    /* entry_only TRUE skips the lock and the unlock. */
    reset();
    ASSERT_EQ(status_$ok, (uint32_t)call(false, true, false));
    ASSERT_EQ(0, pl_calls);
    ASSERT_EQ(0, ud_calls);
    ASSERT_EQ(1, do_calls);

    /* A failed lock aborts before the unlink. */
    reset();
    pl_status_out = 0x000F0006;
    ASSERT_EQ(0x000F0006u, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, re_calls);
}

/*
 * 0x00E51530-0x00E5158A: the second dir_$remove_entry throws its UID away,
 * the handle is released, and only then is the object deleted; the deleted
 * UID is handed back when FILE_$DELETE_OBJ's flag byte is negative.
 */
TEST(unlink_then_delete_then_report_the_uid)
{
    reset();
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, re_calls);
    ASSERT_TRUE(re_uid_ret[0] != &entry_uid_ret);   /* the scratch cell */
    ASSERT_EQ(1, do_calls);
    ASSERT_EQ(0xFF, (unsigned char)do_force);
    ASSERT_EQ(ENTRY_UID.high, deleted_uid_ret.high);
    ASSERT_EQ(ENTRY_UID.low,  deleted_uid_ret.low);

    /* A non-negative flag byte leaves the caller's cell at UID_$NIL. */
    reset();
    do_flag_out = 0;
    ASSERT_EQ(status_$ok, (uint32_t)call(false, false, false));
    ASSERT_EQ(1, do_calls);
    ASSERT_EQ(0u, deleted_uid_ret.high);

    /* A failed unlink releases the handle and returns. */
    reset();
    re_status_out = 0x000E000D;
    ASSERT_EQ(0x000E000Du, (uint32_t)call(false, false, false));
    ASSERT_EQ(0, do_calls);
    ASSERT_EQ(1, rh_calls);
}

int main(void)
{
    printf("dir_$do_op_delete (0x00E5125E) tests\n");

    RUN_TEST(prologue_clears_the_uid_and_opens_the_directory_as_super);
    RUN_TEST(missing_entry);
    RUN_TEST(mount_entry);
    RUN_TEST(entry_uid_is_returned);
    RUN_TEST(soft_link_drop_skips_the_object);
    RUN_TEST(volume_mismatch_falls_back_to_unlink_only);
    RUN_TEST(common_attributes_flag_word);
    RUN_TEST(access_flag_gates);
    RUN_TEST(sub_type_gate);
    RUN_TEST(mount_source_directory_is_refused);
    RUN_TEST(object_rights_check_uses_the_recovered_constants);
    RUN_TEST(acl_status_triage);
    RUN_TEST(delete_right_arithmetic);
    RUN_TEST(directory_is_dereferenced_not_deleted);
    RUN_TEST(object_is_locked_before_the_unlink);
    RUN_TEST(unlink_then_delete_then_report_the_uid);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
