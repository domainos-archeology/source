/*
 * dir/test/test_old_create_diru.c - unit tests for DIR_$OLD_CREATE_DIRU
 * (0x00E571AE)
 *
 * dir/old_create_diru.c is #included below and driven through mocks of every
 * routine it calls.  The behaviours covered are the ones bead source-04ci
 * reported missing: the per-process save/restore of the four NAME_$LOCK_*
 * cells around the create, the argument shapes of dir_$old_create_obj and
 * dir_$old_add_entry, the unconditional pair of DIR_$OLD_SET_DEFAULT_ACL
 * calls plus AST_$TRUNCATE and the UID_$NIL store on the failure path, and
 * the "unlock status wins when it is nonzero" tail.
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
/* Globals the .c under test reads                                      */
/* ------------------------------------------------------------------ */

uint16_t PROC1_$CURRENT;
uid_t    UID_$NIL = { 0, 0 };
uid_t    ACL_$FILE_ACL = { 0x00000006u, 0x00000000u };  /* 0xE17444 */
uid_t    ACL_$DIR_ACL  = { 0x00000601u, 0x00000000u };  /* 0xE1744C */

uint32_t NAME_$LOCK_SLOT[NAME_$MAX_LOCK_PROCS];
int16_t  NAME_$LOCK_MODE[NAME_$MAX_LOCK_PROCS];
uint32_t NAME_$LOCK_HANDLE[NAME_$MAX_LOCK_PROCS];
uid_t    NAME_$LOCK_UID[NAME_$MAX_LOCK_PROCS];

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int8_t   vl_result;
static int      vl_calls;
static uint16_t vl_len_seen;
int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    vl_calls++;
    vl_len_seen = name_len;
    (void)name;
    parsed[0] = 'D';
    *parsed_len = 1;
    return vl_result;
}

static int       lock_calls;
static int16_t   lock_mode_seen;
static int16_t   lock_rights_seen;
static status_$t lock_status;
static uint32_t  lock_handle = 0xDEADBEEFu;
void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    lock_calls++;
    lock_mode_seen = lock_mode;
    lock_rights_seen = acl_rights;
    (void)dir_uid;
    *handle_ret = lock_handle;
    *status_ret = lock_status;
}

static int       unlock_calls;
static status_$t unlock_status_out;
void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    unlock_calls++;
    *status_ret = unlock_status_out;
}

static int exit_super_calls;
void ACL_$EXIT_SUPER(void) { exit_super_calls++; }

/*
 * dir_$old_create_obj and dir_$old_add_entry both scribble on the
 * per-process lock cells the way the real ones do, so the test can see the
 * caller's values being restored afterwards.
 */
static int       create_calls;
static uint16_t  create_type_seen;
static uid_t    *create_uid_seen;
static uint32_t  create_handle_seen;
static status_$t create_status;
void dir_$old_create_obj(uid_t *parent_uid, uint32_t handle, uint16_t type,
                         uid_t *new_dir_uid, status_$t *status_ret)
{
    create_calls++;
    create_type_seen = type;
    create_uid_seen = new_dir_uid;
    create_handle_seen = handle;
    (void)parent_uid;
    NAME_$LOCK_UID[PROC1_$CURRENT].high = 0xBAD00001u;
    NAME_$LOCK_UID[PROC1_$CURRENT].low  = 0xBAD00002u;
    NAME_$LOCK_HANDLE[PROC1_$CURRENT] = 0xBAD00003u;
    NAME_$LOCK_MODE[PROC1_$CURRENT] = 0x0BAD;
    NAME_$LOCK_SLOT[PROC1_$CURRENT] = 0xBAD00004u;
    new_dir_uid->high = 0x0C0FFEE0u;
    new_dir_uid->low  = 0x0C0FFEE1u;
    *status_ret = create_status;
}

static int       add_calls;
static uint16_t  add_type_seen;
static uint16_t  add_flags_seen;
static uint16_t  add_name_len_seen;
static void     *add_uid_seen;
static status_$t add_status;
void dir_$old_add_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                        uint16_t name_len, uint16_t type, void *uid_data,
                        boolean replace_flag, uint8_t *result,
                        status_$t *status_ret)
{
    add_calls++;
    add_type_seen = type;
    add_flags_seen = (uint16_t)(uint8_t)replace_flag;
    add_name_len_seen = name_len;
    add_uid_seen = uid_data;
    (void)dir_uid; (void)handle; (void)name;
    *(uint32_t *)result = 0;
    *status_ret = add_status;
}

#define MAX_ACL_CALLS 4
static int    acl_calls;
static uid_t *acl_type_seen[MAX_ACL_CALLS];
static uid_t *acl_uid_seen[MAX_ACL_CALLS];
void DIR_$OLD_SET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_uid,
                              status_$t *status_ret)
{
    if (acl_calls < MAX_ACL_CALLS) {
        acl_type_seen[acl_calls] = acl_type;
        acl_uid_seen[acl_calls] = acl_uid;
    }
    acl_calls++;
    (void)dir_uid;
    *status_ret = status_$ok;
}

static int      trunc_calls;
static uint32_t trunc_size_seen;
static uint16_t trunc_flags_seen;
void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    trunc_calls++;
    trunc_size_seen = new_size;
    trunc_flags_seen = flags;
    (void)uid;
    *result = 0;
    *status = status_$ok;
}

#include "../old_create_diru.c"

/* ------------------------------------------------------------------ */

static uid_t     parent = { 0x11111111u, 0x22222222u };
static uid_t     out_uid;
static status_$t st;
static uint16_t  name_len;
static char      the_name[] = "DIR";

static const uid_t SAVED_UID    = { 0x0A0A0A0Au, 0x0B0B0B0Bu };
static const uint32_t SAVED_HND = 0x1234ABCDu;
static const int16_t  SAVED_MOD = 0x0042;
static const uint32_t SAVED_SLT = 0x55AA55AAu;

static void reset(void)
{
    memset(NAME_$LOCK_SLOT, 0, sizeof(NAME_$LOCK_SLOT));
    memset(NAME_$LOCK_MODE, 0, sizeof(NAME_$LOCK_MODE));
    memset(NAME_$LOCK_HANDLE, 0, sizeof(NAME_$LOCK_HANDLE));
    memset(NAME_$LOCK_UID, 0, sizeof(NAME_$LOCK_UID));
    PROC1_$CURRENT = 5;
    NAME_$LOCK_UID[5] = SAVED_UID;
    NAME_$LOCK_HANDLE[5] = SAVED_HND;
    NAME_$LOCK_MODE[5] = SAVED_MOD;
    NAME_$LOCK_SLOT[5] = SAVED_SLT;

    vl_result = (int8_t)0xFF;   /* Domain TRUE: the leaf is valid */
    vl_calls = 0;
    lock_calls = 0; lock_status = status_$ok;
    lock_mode_seen = 0; lock_rights_seen = 0;
    unlock_calls = 0; unlock_status_out = status_$ok;
    exit_super_calls = 0;
    create_calls = 0; create_status = status_$ok; create_uid_seen = NULL;
    create_type_seen = 0xFFFF; create_handle_seen = 0;
    add_calls = 0; add_status = status_$ok; add_type_seen = 0xFFFF;
    add_flags_seen = 0xFFFF; add_uid_seen = NULL; add_name_len_seen = 0xFFFF;
    acl_calls = 0;
    trunc_calls = 0; trunc_size_seen = 0xFFFFFFFFu; trunc_flags_seen = 0xFFFF;

    out_uid.high = 0x99999999u;
    out_uid.low  = 0x88888888u;
    st = 0x5A5A5A5A;
    name_len = 3;
}

/* 0x00E571E8: an invalid leaf returns without entering super mode. */
TEST(an_invalid_leaf_returns_before_the_lock)
{
    reset();
    vl_result = 0;                  /* Domain FALSE */
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(status_$naming_invalid_leaf, st);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, exit_super_calls);
}

/* 0x00E571F2: the pushed longword 0x00040002 is two word arguments. */
TEST(the_lock_takes_mode_4_and_rights_2)
{
    reset();
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(4, lock_mode_seen);
    ASSERT_EQ(2, lock_rights_seen);
}

/* 0x00E57208: tst.l (A4) - a failed lock exits super mode and returns. */
TEST(a_failed_lock_exits_super_without_unlocking)
{
    reset();
    lock_status = status_$naming_directory_locked;
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(status_$naming_directory_locked, st);
    ASSERT_EQ(0, create_calls);
    ASSERT_EQ(0, unlock_calls);
    ASSERT_EQ(1, exit_super_calls);
}

/*
 * 0x00E5720E-0x00E57246 saves and 0x00E572EE-0x00E57326 restores the four
 * per-process cells, so whatever dir_$old_create_obj leaves behind is undone.
 */
TEST(the_per_process_lock_state_is_saved_and_restored)
{
    reset();
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(SAVED_UID.high, NAME_$LOCK_UID[5].high);
    ASSERT_EQ(SAVED_UID.low,  NAME_$LOCK_UID[5].low);
    ASSERT_EQ(SAVED_HND, NAME_$LOCK_HANDLE[5]);
    ASSERT_EQ((uint16_t)SAVED_MOD, (uint16_t)NAME_$LOCK_MODE[5]);
    ASSERT_EQ(SAVED_SLT, NAME_$LOCK_SLOT[5]);
}

/* The restore happens even when dir_$old_create_obj fails (0x00E57264). */
TEST(the_lock_state_is_restored_after_a_failed_create)
{
    reset();
    create_status = status_$naming_bad_directory;
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(0, add_calls);
    ASSERT_EQ(SAVED_HND, NAME_$LOCK_HANDLE[5]);
    ASSERT_EQ(SAVED_SLT, NAME_$LOCK_SLOT[5]);
    ASSERT_EQ(status_$naming_bad_directory, st);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(1, exit_super_calls);
}

/*
 * 0x00E5724A / 0x00E57268: the type word is 1 on both calls and the created
 * UID goes straight into the caller's cell - there is no local copy.
 */
TEST(create_and_add_both_use_type_1_on_the_callers_uid)
{
    reset();
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(1, create_type_seen);
    ASSERT_TRUE(create_uid_seen == &out_uid);
    ASSERT_EQ(lock_handle, create_handle_seen);
    ASSERT_EQ(1, add_type_seen);
    ASSERT_EQ(0, add_flags_seen);
    ASSERT_EQ(1, add_name_len_seen);     /* the PARSED length */
    ASSERT_TRUE(add_uid_seen == (void *)&out_uid);
    ASSERT_EQ(0x0C0FFEE0u, out_uid.high);
}

/*
 * 0x00E57292-0x00E572EA: a failed add backs the object out with TWO
 * unconditional DIR_$OLD_SET_DEFAULT_ACL calls (ACL_$FILE_ACL then
 * ACL_$DIR_ACL, both against &UID_$NIL), AST_$TRUNCATE(0, flags 3) and a
 * UID_$NIL store into the caller's UID.
 */
TEST(a_failed_add_backs_the_new_object_out)
{
    reset();
    add_status = status_$name_already_exists;
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(2, acl_calls);
    ASSERT_TRUE(acl_type_seen[0] == &ACL_$FILE_ACL);
    ASSERT_TRUE(acl_uid_seen[0]  == &UID_$NIL);
    ASSERT_TRUE(acl_type_seen[1] == &ACL_$DIR_ACL);
    ASSERT_TRUE(acl_uid_seen[1]  == &UID_$NIL);
    ASSERT_EQ(1, trunc_calls);
    ASSERT_EQ(0, trunc_size_seen);
    ASSERT_EQ(3, trunc_flags_seen);
    ASSERT_EQ(0, out_uid.high);
    ASSERT_EQ(0, out_uid.low);
    ASSERT_EQ(status_$name_already_exists, st);
}

/* Nothing is backed out when the add succeeded. */
TEST(a_successful_add_backs_nothing_out)
{
    reset();
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(0, acl_calls);
    ASSERT_EQ(0, trunc_calls);
    ASSERT_EQ(status_$ok, st);
}

/*
 * 0x00E57334: `move.l (-0x48,A6),D1 / beq / move.l D1,(A4)` - this tail
 * replaces the status whenever the UNLOCK status is nonzero, even over a
 * status the body had already set.
 */
TEST(a_nonzero_unlock_status_replaces_the_bodys_status)
{
    reset();
    add_status = status_$name_already_exists;
    unlock_status_out = status_$naming_internal_error;
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(status_$naming_internal_error, st);
}

/* A zero unlock status leaves the body's status alone. */
TEST(a_zero_unlock_status_leaves_the_status_alone)
{
    reset();
    add_status = status_$name_already_exists;
    unlock_status_out = status_$ok;
    DIR_$OLD_CREATE_DIRU(&parent, the_name, &name_len, &out_uid, &st);
    ASSERT_EQ(status_$name_already_exists, st);
    ASSERT_EQ(1, exit_super_calls);
}

int main(void)
{
    printf("DIR_$OLD_CREATE_DIRU (0x00E571AE) tests\n");

    RUN_TEST(an_invalid_leaf_returns_before_the_lock);
    RUN_TEST(the_lock_takes_mode_4_and_rights_2);
    RUN_TEST(a_failed_lock_exits_super_without_unlocking);
    RUN_TEST(the_per_process_lock_state_is_saved_and_restored);
    RUN_TEST(the_lock_state_is_restored_after_a_failed_create);
    RUN_TEST(create_and_add_both_use_type_1_on_the_callers_uid);
    RUN_TEST(a_failed_add_backs_the_new_object_out);
    RUN_TEST(a_successful_add_backs_nothing_out);
    RUN_TEST(a_nonzero_unlock_status_replaces_the_bodys_status);
    RUN_TEST(a_zero_unlock_status_leaves_the_status_alone);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
