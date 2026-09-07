/*
 * name/test/test_old_add_link.c - Unit tests for name_$old_add_link
 * (0x00E5674C)
 *
 * name/old_add_link.c is #included below and driven through mocks of every
 * routine it calls.  The behaviours covered are the ones bead source-ns3b
 * raised (the missing ACL_$RIGHTS call at 0x00E567C0 and its three pea-cell
 * constants) plus the rest of the function's basic blocks.
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

#include "name/name_internal.h"
#include "dir/dir.h"

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static const uid_t DIR_UID  = { 0x11111111u, 0x22222222u };
static const uid_t FILE_UID = { 0x33333333u, 0x44444444u };

/* --- AST_$GET_LOCATION.  Call 1 is the FILE, call 2 is the DIRECTORY. --- */
static int             gl_calls;
static file_$obj_loc_t gl_out[2];
static status_$t       gl_status_out[2];
static uid_t           gl_uid_seen[2];

void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *vol_uid_out,
                       status_$t *status)
{
    int i = gl_calls < 1 ? 0 : 1;
    (void)flags; (void)unused; (void)vol_uid_out;
    gl_uid_seen[i] = loc_rec->uid;
    gl_calls++;
    {
        uid_t saved = loc_rec->uid;
        *loc_rec = gl_out[i];
        loc_rec->uid = saved;
    }
    *status = gl_status_out[i];
}

/* --- ACL_$RIGHTS --- */
static int       ar_calls;
static uid_t     ar_uid;
static boolean   ar_ignore_super;
static uint32_t  ar_mask;
static int16_t   ar_opts;
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
    return 0;
}

static int nc_calls;
void NAME_CONVERT_ACL_STATUS(status_$t *status_ret)
{
    nc_calls++;
    *status_ret = 0x000E0013;
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
    parsed[0] = 'y';
    return vl_result;
}

/* --- REM_FILE_$NAME_ADD_HARD_LINKU --- */
static int       rh_calls;
static status_$t rh_status_out[3];
static uint16_t  rh_name_len;
void REM_FILE_$NAME_ADD_HARD_LINKU(void *addr_info, uid_t *dir_uid,
                                   char *name, uint16_t name_len,
                                   uid_t *file_uid, status_$t *status)
{
    int i = rh_calls < 2 ? rh_calls : 2;
    (void)addr_info; (void)dir_uid; (void)name; (void)file_uid;
    rh_name_len = name_len;
    rh_calls++;
    *status = rh_status_out[i];
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

/* --- AST_$SET_ATTRIBUTE --- */
static int       sa_calls;
static uint16_t  sa_attr[3];
static uint16_t  sa_value[3];
static status_$t sa_status_out;
void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status)
{
    int i = sa_calls < 2 ? sa_calls : 2;
    (void)uid;
    sa_attr[i]  = attr_id;
    sa_value[i] = *(uint16_t *)value;
    sa_calls++;
    *status = sa_status_out;
}

/* --- name_$old_add_link_local --- */
static int       la_calls;
static int16_t   la_acl_rights;
static uint16_t  la_name_len;
static status_$t la_status_out;
void name_$old_add_link_local(uid_t *dir_uid, int16_t acl_rights, char *name,
                              uint16_t name_len, uid_t *file_uid,
                              status_$t *status_ret)
{
    (void)dir_uid; (void)name; (void)file_uid;
    la_calls++;
    la_acl_rights = acl_rights;
    la_name_len = name_len;
    *status_ret = la_status_out;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

#include "../old_add_link.c"

/* ------------------------------------------------------------------ */

static void reset(void)
{
    memset(gl_out, 0, sizeof(gl_out));
    memset(rh_status_out, 0, sizeof(rh_status_out));
    memset(sa_attr, 0, sizeof(sa_attr));
    memset(sa_value, 0, sizeof(sa_value));
    gl_calls = ar_calls = nc_calls = vl_calls = 0;
    rh_calls = rl_calls = sa_calls = la_calls = 0;
    gl_status_out[0] = gl_status_out[1] = status_$ok;
    ar_status_out = status_$ok;
    rl_status_out = status_$ok;
    sa_status_out = status_$ok;
    la_status_out = status_$ok;
    vl_result = -1;
    /* Both objects local and on the same volume. */
    gl_out[0].volume = 4;
    gl_out[1].volume = 4;
    gl_out[0].flags  = 0;
    gl_out[1].flags  = 0;
}

static status_$t call(boolean hard_link)
{
    uid_t dir = DIR_UID, file = FILE_UID;
    status_$t st = 0xdeadbeef;
    name_$old_add_link(&dir, "ab", 2, &file, hard_link, &st);
    return st;
}

/* ------------------------------------------------------------------ */

/*
 * 0x00E56764: the FIRST AST_$GET_LOCATION is seeded with the TARGET's UID,
 * the second (0x00E567DE) with the DIRECTORY's.
 */
TEST(the_two_location_lookups_use_the_right_uids)
{
    reset();
    (void)call(false);
    ASSERT_EQ(2, gl_calls);
    ASSERT_EQ(FILE_UID.high, gl_uid_seen[0].high);
    ASSERT_EQ(FILE_UID.low,  gl_uid_seen[0].low);
    ASSERT_EQ(DIR_UID.high,  gl_uid_seen[1].high);
    ASSERT_EQ(DIR_UID.low,   gl_uid_seen[1].low);
}

/*
 * 0x00E56794-0x00E567AC: a hard-link add gives up on any location failure;
 * a plain add tolerates exactly file_$object_not_found.
 */
TEST(missing_target_is_tolerated_only_for_a_plain_add)
{
    reset();
    gl_status_out[0] = file_$object_not_found;
    ASSERT_EQ(file_$object_not_found, (uint32_t)call(true));
    ASSERT_EQ(0, ar_calls);

    reset();
    gl_status_out[0] = file_$object_not_found;
    ASSERT_EQ(status_$ok, (uint32_t)call(false));
    ASSERT_EQ(1, ar_calls);
    ASSERT_EQ(0, sa_calls);     /* target_exists FALSE -> no link-count bump */
    ASSERT_EQ(1, la_calls);

    reset();
    gl_status_out[0] = 0x000F0002;      /* some other failure */
    ASSERT_EQ(0x000F0002u, (uint32_t)call(false));
    ASSERT_EQ(0, ar_calls);
}

/*
 * source-ns3b: the missing call at 0x00E567C0.  It is made on the PARENT
 * DIRECTORY with ignore_super = 0x00E54B28 (FALSE), required mask
 * 0x00E56946 (0x02) and option flags 0x00E54B26 (1 = directory).
 */
TEST(directory_rights_are_checked_with_the_recovered_constants)
{
    reset();
    (void)call(false);

    ASSERT_EQ(1, ar_calls);
    ASSERT_EQ(DIR_UID.high, ar_uid.high);
    ASSERT_EQ(DIR_UID.low,  ar_uid.low);
    ASSERT_EQ(0x00, (unsigned char)ar_ignore_super);
    ASSERT_EQ(0x00000002u, ar_mask);
    ASSERT_EQ(1, ar_opts);
}

/* 0x00E567CA-0x00E567DA: an ACL failure is translated, and nothing else runs. */
TEST(acl_failure_is_translated_and_stops_the_operation)
{
    reset();
    ar_status_out = 0x00230001;
    ASSERT_EQ(0x000E0013u, (uint32_t)call(false));
    ASSERT_EQ(1, nc_calls);
    ASSERT_EQ(1, gl_calls);     /* the directory lookup never happens */
    ASSERT_EQ(0, la_calls);
}

/* 0x00E5680C: a failed directory lookup aborts before either path. */
TEST(directory_location_failure_aborts)
{
    reset();
    gl_status_out[1] = 0x000F0002;
    ASSERT_EQ(0x000F0002u, (uint32_t)call(false));
    ASSERT_EQ(0, la_calls);
    ASSERT_EQ(0, rh_calls);
}

/*
 * 0x00E56814-0x00E5681E: the remote path needs BOTH a target that exists and
 * a directory whose location record has bit 7 set.
 */
TEST(remote_path_gate)
{
    reset();
    gl_out[1].flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    ASSERT_EQ(status_$ok, (uint32_t)call(false));
    ASSERT_EQ(1, vl_calls);
    ASSERT_EQ(1, rh_calls);
    ASSERT_EQ(2, rh_name_len);
    ASSERT_EQ(0, la_calls);

    /* Target missing -> local path even though the directory is remote. */
    reset();
    gl_out[1].flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    gl_status_out[0] = file_$object_not_found;
    ASSERT_EQ(status_$ok, (uint32_t)call(false));
    ASSERT_EQ(0, rh_calls);
    ASSERT_EQ(1, la_calls);
}

/* 0x00E5683A-0x00E56846: an invalid leaf name is 0x000E000B. */
TEST(remote_path_validates_the_leaf)
{
    reset();
    gl_out[1].flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    vl_result = 0;
    ASSERT_EQ(0x000E000Bu, (uint32_t)call(false));
    ASSERT_EQ(0, rh_calls);
}

/*
 * 0x00E5686A-0x00E568AE: one retry after file_$comms_problem_with_remote_node,
 * gated on FILE_$READ_LOCK_ENTRYUI reporting "not locked by this process".
 */
TEST(remote_retry)
{
    reset();
    gl_out[1].flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    rh_status_out[0] = file_$comms_problem_with_remote_node;
    rl_status_out    = file_$object_not_locked_by_this_process;
    (void)call(false);
    ASSERT_EQ(1, rl_calls);
    ASSERT_EQ(2, rh_calls);

    reset();
    gl_out[1].flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    rh_status_out[0] = file_$comms_problem_with_remote_node;
    rl_status_out    = status_$ok;
    (void)call(false);
    ASSERT_EQ(1, rl_calls);
    ASSERT_EQ(1, rh_calls);
}

/* 0x00E568B2-0x00E568C4: file_$bad_reply becomes "cannot be done from here". */
TEST(bad_reply_is_remapped)
{
    reset();
    gl_out[1].flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    rh_status_out[0] = file_$bad_reply_received_from_remote_node;
    ASSERT_EQ(file_$op_cannot_perform_here, (uint32_t)call(false));
    ASSERT_EQ(0x000F000Bu, (uint32_t)file_$op_cannot_perform_here);
}

/*
 * 0x00E568CC-0x00E568F6: the link count is bumped (attribute 6, value 1) only
 * when the target exists, the volumes match and the target is local.
 */
TEST(link_count_bump_conditions)
{
    reset();
    ASSERT_EQ(status_$ok, (uint32_t)call(false));
    ASSERT_EQ(1, sa_calls);
    ASSERT_EQ(6, sa_attr[0]);
    ASSERT_EQ(1, sa_value[0]);

    /* Different volumes -> no bump. */
    reset();
    gl_out[0].volume = 4;
    gl_out[1].volume = 5;
    ASSERT_EQ(status_$ok, (uint32_t)call(false));
    ASSERT_EQ(0, sa_calls);
    ASSERT_EQ(1, la_calls);

    /* Remote target -> no bump. */
    reset();
    gl_out[0].flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    ASSERT_EQ(status_$ok, (uint32_t)call(false));
    ASSERT_EQ(0, sa_calls);
}

/* 0x00E568FA: a failed bump stops before the local add. */
TEST(failed_link_count_bump_stops_the_add)
{
    reset();
    sa_status_out = 0x00030007;
    ASSERT_EQ(0x00030007u, (uint32_t)call(false));
    ASSERT_EQ(1, sa_calls);
    ASSERT_EQ(0, la_calls);
}

/*
 * 0x00E56900-0x00E5692E: the local add gets a constant zero second argument,
 * and a failure rolls the link count back with attribute 7 - but the caller
 * still sees the ADD's status, not the rollback's.
 */
TEST(local_add_and_rollback)
{
    reset();
    ASSERT_EQ(status_$ok, (uint32_t)call(false));
    ASSERT_EQ(1, la_calls);
    ASSERT_EQ(0, la_acl_rights);
    ASSERT_EQ(2, la_name_len);
    ASSERT_EQ(1, sa_calls);         /* only the bump */

    reset();
    la_status_out = 0x000E0003;     /* name already exists */
    ASSERT_EQ(0x000E0003u, (uint32_t)call(false));
    ASSERT_EQ(2, sa_calls);
    ASSERT_EQ(7, sa_attr[1]);
    ASSERT_EQ(1, sa_value[1]);
}

int main(void)
{
    printf("name_$old_add_link (0x00E5674C) tests\n");

    RUN_TEST(the_two_location_lookups_use_the_right_uids);
    RUN_TEST(missing_target_is_tolerated_only_for_a_plain_add);
    RUN_TEST(directory_rights_are_checked_with_the_recovered_constants);
    RUN_TEST(acl_failure_is_translated_and_stops_the_operation);
    RUN_TEST(directory_location_failure_aborts);
    RUN_TEST(remote_path_gate);
    RUN_TEST(remote_path_validates_the_leaf);
    RUN_TEST(remote_retry);
    RUN_TEST(bad_reply_is_remapped);
    RUN_TEST(link_count_bump_conditions);
    RUN_TEST(failed_link_count_bump_stops_the_add);
    RUN_TEST(local_add_and_rollback);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
