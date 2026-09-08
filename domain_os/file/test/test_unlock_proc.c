/*
 * file/test/test_unlock_proc.c - FILE_$UNLOCK_PROC (0x00E60E3E)
 *
 * The regressions source-77ju is about:
 *
 *  1. `pea (0x156,PC)` at 0x00E60E78 addresses 0x00E60E7A + 0x156 =
 *     0x00E60FD0, a zero byte, which is PROC2_$FIND_ASID's second argument -
 *     the callee dereferences it (`movea.l (0xc,A6),A0` / `tst.b (A0)` at
 *     0x00E40756).  The tree passed nil.  It is the SAME cell ACL_$RIGHTS
 *     gets as `ignore_super` from `pea (0x116,PC)` at 0x00E60EB8.
 *
 *  2. The trailing `*status_ret = status_$ok` was invented.  After the local
 *     `dbf` loop exhausts, 0x00E60F26 branches straight to the epilogue with
 *     the status still 0x000F0005; after the remote walk, 0x00E60FBC clears
 *     ONLY 0x000F000C and returns every other status as it stands.
 *
 * The real file/unlock_proc.c is #included at the bottom; everything it calls
 * is mocked here.
 */

#include <stdio.h>
#include <string.h>

#include "file/file_internal.h"
#include "acl/acl.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-50s ", #name);          \
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

#define ASSERT_PTR_EQ(expected, actual) do {                             \
    const void *_e = (const void *)(expected);                           \
    const void *_a = (const void *)(actual);                             \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected %p, got %p at line %d\n",           \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

/* ==========================================================================
 * Globals the code under test reaches
 * ========================================================================== */

uid_t    UID_$NIL = { 0, 0 };
uint32_t NODE_$ME = 0x000ABCDE;
uint16_t PROC1_$AS_ID = 5;

/*
 * The per-process lock count lives at 0xEA3DC4 + asid*2 in the image;
 * FILE_$PROC_LOT_COUNT resolves to FILE_$LOCK_TABLE2[] on a host build, so
 * that array is what the tests seed.
 */
uint16_t FILE_$LOCK_TABLE2[FILE_LOCK_TABLE_ENTRIES];
#define host_lot_count FILE_$LOCK_TABLE2

/* ==========================================================================
 * Mock bookkeeping
 * ========================================================================== */

static int        find_asid_calls;
static void      *find_asid_arg2;
static uint16_t   find_asid_result;
static status_$t  find_asid_status;

static int        acl_rights_calls;
static void      *acl_rights_ignore_super;
static void      *acl_rights_mask;
static void      *acl_rights_opts;
static status_$t  acl_rights_status;

static int        shutwired_calls;

static int        priv_unlock_calls;
static int32_t    priv_unlock_slot[16];
static uint16_t   priv_unlock_asid[16];
static boolean    priv_unlock_by_key[16];
static status_$t  priv_unlock_status[16];

static int        read_entry_calls;
static status_$t  read_entry_status[16];
static file_lock_info_internal_t read_entry_info[16];

/* ==========================================================================
 * Mocks
 * ========================================================================== */

uint16_t PROC2_$FIND_ASID(uid_t *proc_uid, int8_t *param_2,
                          status_$t *status_ret)
{
    (void)proc_uid;
    find_asid_calls++;
    find_asid_arg2 = param_2;
    /* the real callee dereferences this argument */
    if (param_2 == NULL) {
        printf("\n      PROC2_$FIND_ASID got a nil second argument\n");
        current_failed = 1;
        tests_failed++;
    }
    *status_ret = find_asid_status;
    return find_asid_result;
}

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required,
                     int16_t *opts, status_$t *status_ret)
{
    (void)uid;
    acl_rights_calls++;
    acl_rights_ignore_super = ignore_super;
    acl_rights_mask = required;
    acl_rights_opts = opts;
    *status_ret = acl_rights_status;
    return 0;
}

void OS_PROC_SHUTWIRED(status_$t *status)
{
    (void)status;
    shutwired_calls++;
}

boolean FILE_$PRIV_UNLOCK(uid_t *file_uid, int32_t lock_slot,
                          uint16_t lock_mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv_out, status_$t *status_ret)
{
    (void)file_uid; (void)lock_mode; (void)key; (void)rem_key; (void)rem_node;
    if (priv_unlock_calls < 16) {
        priv_unlock_slot[priv_unlock_calls]   = lock_slot;
        priv_unlock_asid[priv_unlock_calls]   = asid;
        priv_unlock_by_key[priv_unlock_calls] = by_key;
        *status_ret = priv_unlock_status[priv_unlock_calls];
    } else {
        *status_ret = status_$ok;
    }
    *dtv_out = 0;
    priv_unlock_calls++;
    return 0;
}

void FILE_$READ_LOCK_ENTRYI(uid_t *uid, uint16_t *index,
                            file_lock_info_internal_t *info,
                            status_$t *status_ret)
{
    (void)uid;
    if (read_entry_calls < 16) {
        *info = read_entry_info[read_entry_calls];
        *status_ret = read_entry_status[read_entry_calls];
    } else {
        memset(info, 0, sizeof(*info));
        *status_ret = file_$obj_not_locked_by_this_process;
    }
    read_entry_calls++;
    *index = (uint16_t)(*index + 1);
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../unlock_proc.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static uid_t file_uid  = { 0x11111111u, 0x22222222u };
static uid_t proc_uid  = { 0x33333333u, 0x000ABCDEu };  /* node = NODE_$ME */
static uint16_t mode   = 0;

static void reset(void)
{
    int i;

    memset(FILE_$LOCK_TABLE2, 0, sizeof(FILE_$LOCK_TABLE2));
    PROC1_$AS_ID = 5;
    NODE_$ME = 0x000ABCDE;

    find_asid_calls = 0;
    find_asid_arg2 = NULL;
    find_asid_result = 5;
    find_asid_status = status_$ok;

    acl_rights_calls = 0;
    acl_rights_ignore_super = NULL;
    acl_rights_mask = NULL;
    acl_rights_opts = NULL;
    acl_rights_status = status_$ok;

    shutwired_calls = 0;
    priv_unlock_calls = 0;
    read_entry_calls = 0;

    for (i = 0; i < 16; i++) {
        priv_unlock_slot[i] = 0;
        priv_unlock_asid[i] = 0;
        priv_unlock_by_key[i] = 0;
        priv_unlock_status[i] = file_$object_not_locked_by_this_process;
        read_entry_status[i] = file_$obj_not_locked_by_this_process;
        memset(&read_entry_info[i], 0, sizeof(read_entry_info[i]));
    }

    file_uid = (uid_t){ 0x11111111u, 0x22222222u };
    proc_uid = (uid_t){ 0x33333333u, 0x00012345u };   /* NOT this node */
    mode = 0;

    /* the constant cells must survive a run */
    file_$unlock_proc_false_00e60fd0 = false;
    file_$unlock_proc_acl_opts_00e60fd2 = 0;
    file_$unlock_proc_rights_00e60fd4 = 0x00000008;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Image bytes at 0x00E60FD0..0x00E60FD7: 00 00 00 00 00 00 00 08 */
TEST(constant_cell_values)
{
    reset();
    ASSERT_EQ(0x00, file_$unlock_proc_false_00e60fd0);
    ASSERT_EQ(0x0000, file_$unlock_proc_acl_opts_00e60fd2);
    ASSERT_EQ(0x00000008, file_$unlock_proc_rights_00e60fd4);
}

/* Regression 1: PROC2_$FIND_ASID gets the 0x00E60FD0 cell, never nil. */
TEST(find_asid_gets_the_zero_byte_cell)
{
    status_$t status = 0;

    reset();
    find_asid_result = 7;                    /* a different ASID than 5 */
    FILE_$UNLOCK_PROC(&proc_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(1, find_asid_calls);
    ASSERT_PTR_EQ(&file_$unlock_proc_false_00e60fd0, find_asid_arg2);
    ASSERT_EQ(0x00, *(const int8_t *)find_asid_arg2);
}

/* One cell, two callees (0x00E60E78 and 0x00E60EB8). */
TEST(find_asid_and_acl_rights_share_one_cell)
{
    status_$t status = 0;

    reset();
    find_asid_result = 7;
    FILE_$UNLOCK_PROC(&proc_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(1, acl_rights_calls);
    ASSERT_PTR_EQ(find_asid_arg2, acl_rights_ignore_super);
    ASSERT_PTR_EQ(&file_$unlock_proc_rights_00e60fd4, acl_rights_mask);
    ASSERT_PTR_EQ(&file_$unlock_proc_acl_opts_00e60fd2, acl_rights_opts);
    ASSERT_EQ(0x00000008, *(const uint32_t *)acl_rights_mask);
}

/* 0x00E60E5C-0x00E60E74: UID_$NIL means "the current process", and then
 * 0x00E60EA6 finds the ASID unchanged so no ACL check runs. */
TEST(nil_proc_uid_uses_the_current_asid)
{
    status_$t status = 0;
    uid_t nil_uid = { 0, 0 };

    reset();
    host_lot_count[5] = 0;
    FILE_$UNLOCK_PROC(&nil_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(0, find_asid_calls);
    ASSERT_EQ(0, acl_rights_calls);
    ASSERT_EQ(status_$ok, status);      /* empty table, status untouched */
}

/*
 * THE regression: the local `dbf` loop exhausting leaves 0x000F0005 in the
 * status.  0x00E60F26 goes straight to the epilogue.
 */
TEST(local_loop_exhausted_keeps_not_locked_status)
{
    status_$t status = 0;
    uid_t nil_uid = { 0, 0 };

    reset();
    host_lot_count[5] = 3;
    /* every slot reports "not locked by this process", so the loop runs out */
    FILE_$UNLOCK_PROC(&nil_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(3, priv_unlock_calls);
    ASSERT_EQ(1, priv_unlock_slot[0]);
    ASSERT_EQ(2, priv_unlock_slot[1]);
    ASSERT_EQ(3, priv_unlock_slot[2]);
    ASSERT_EQ(5, priv_unlock_asid[0]);
    ASSERT_EQ(0, priv_unlock_by_key[0]);
    ASSERT_EQ(file_$object_not_locked_by_this_process, status);  /* 0x000F0005 */
}

/* 0x00E60F16-0x00E60F1C: any other status stops the loop and is returned. */
TEST(local_loop_returns_the_first_other_status)
{
    status_$t status = 0;
    uid_t nil_uid = { 0, 0 };

    reset();
    host_lot_count[5] = 4;
    priv_unlock_status[1] = status_$ok;
    FILE_$UNLOCK_PROC(&nil_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(2, priv_unlock_calls);
    ASSERT_EQ(status_$ok, status);

    reset();
    host_lot_count[5] = 4;
    priv_unlock_status[0] = 0x000F0006;
    FILE_$UNLOCK_PROC(&nil_uid, &file_uid, &mode, 0, &status);
    ASSERT_EQ(1, priv_unlock_calls);
    ASSERT_EQ(0x000F0006, status);
}

/* 0x00E60EE8-0x00E60EF0: an empty table leaves the status alone. */
TEST(empty_lock_table_returns_the_incoming_status)
{
    status_$t status = 0;
    uid_t nil_uid = { 0, 0 };

    reset();
    host_lot_count[5] = 0;
    FILE_$UNLOCK_PROC(&nil_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(0, priv_unlock_calls);
    ASSERT_EQ(status_$ok, status);
}

/*
 * THE other regression: after the remote walk only 0x000F000C is forgiven.
 */
TEST(remote_walk_forgives_only_000f000c)
{
    status_$t status = 0;

    reset();
    find_asid_status = 0x000F0009;      /* not found ... */
    proc_uid.low = 0x00054321u;         /* ... and not this node -> asid 0 */
    read_entry_status[0] = file_$obj_not_locked_by_this_process;
    FILE_$UNLOCK_PROC(&proc_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(1, read_entry_calls);
    ASSERT_EQ(status_$ok, status);      /* 0x00E60FC4 clr.l (A2) */
}

TEST(remote_walk_returns_any_other_status_unchanged)
{
    status_$t status = 0;

    reset();
    find_asid_status = 0x000F0009;
    proc_uid.low = 0x00054321u;
    read_entry_status[0] = 0x000F0011;  /* not 0 and not 0x000F000C */
    FILE_$UNLOCK_PROC(&proc_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(1, read_entry_calls);
    ASSERT_EQ(0x000F0011, status);      /* 0x00E60FC2 bne - returned as is */
}

/* 0x00E60F5C-0x00E60F62: only entries whose owner node matches are unlocked,
 * and 0x00E60FAC-0x00E60FB4 turns 0x000F0005 back into "keep going". */
TEST(remote_walk_unlocks_the_matching_entry)
{
    status_$t status = 0;

    reset();
    find_asid_status = 0x000F0009;
    proc_uid.low = 0x00054321u;

    /* entry 0: a different node - skipped */
    read_entry_status[0] = status_$ok;
    read_entry_info[0].owner_node = 0x00099999u;

    /* entry 1: our node, our file, mode 0 matches anything */
    read_entry_status[1] = status_$ok;
    read_entry_info[1].owner_node = 0x00054321u;
    read_entry_info[1].file_uid = file_uid;
    read_entry_info[1].mode = 4;
    read_entry_info[1].sequence = 0x1234;
    read_entry_info[1].context = 0xCAFEBABEu;
    priv_unlock_status[0] = file_$object_not_locked_by_this_process;

    /* entry 2: end of table */
    read_entry_status[2] = file_$obj_not_locked_by_this_process;

    FILE_$UNLOCK_PROC(&proc_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(3, read_entry_calls);
    ASSERT_EQ(1, priv_unlock_calls);
    ASSERT_EQ(0, priv_unlock_slot[0]);      /* clr.l -(SP) */
    ASSERT_EQ(0, priv_unlock_asid[0]);      /* clr.w -(SP) */
    ASSERT_EQ((boolean)-1, priv_unlock_by_key[0]);  /* st -(SP) */
    ASSERT_EQ(status_$ok, status);
}

/* 0x00E60E8E-0x00E60EA0: a UID naming THIS node ends the call at once. */
TEST(not_found_on_this_node_returns_ok)
{
    status_$t status = 0;

    reset();
    find_asid_status = 0x000F0009;
    proc_uid.low = NODE_$ME;
    FILE_$UNLOCK_PROC(&proc_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, acl_rights_calls);
    ASSERT_EQ(0, read_entry_calls);
    ASSERT_EQ(0, priv_unlock_calls);
}

/* 0x00E60EC8-0x00E60ED2: a failing ACL check is handed to OS_PROC_SHUTWIRED
 * and then returned unchanged. */
TEST(acl_failure_shuts_wired_and_keeps_the_status)
{
    status_$t status = 0;

    reset();
    find_asid_result = 7;
    acl_rights_status = 0x00230001;
    FILE_$UNLOCK_PROC(&proc_uid, &file_uid, &mode, 0, &status);

    ASSERT_EQ(1, shutwired_calls);
    ASSERT_EQ(0x00230001, status);
    ASSERT_EQ(0, priv_unlock_calls);
}

int main(void)
{
    printf("FILE_$UNLOCK_PROC tests\n");
    RUN_TEST(constant_cell_values);
    RUN_TEST(find_asid_gets_the_zero_byte_cell);
    RUN_TEST(find_asid_and_acl_rights_share_one_cell);
    RUN_TEST(nil_proc_uid_uses_the_current_asid);
    RUN_TEST(local_loop_exhausted_keeps_not_locked_status);
    RUN_TEST(local_loop_returns_the_first_other_status);
    RUN_TEST(empty_lock_table_returns_the_incoming_status);
    RUN_TEST(remote_walk_forgives_only_000f000c);
    RUN_TEST(remote_walk_returns_any_other_status_unchanged);
    RUN_TEST(remote_walk_unlocks_the_matching_entry);
    RUN_TEST(not_found_on_this_node_returns_ok);
    RUN_TEST(acl_failure_shuts_wired_and_keeps_the_status);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
