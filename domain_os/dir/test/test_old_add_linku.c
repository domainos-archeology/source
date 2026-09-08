/*
 * dir/test/test_old_add_linku.c - unit tests for DIR_$OLD_ADD_LINKU
 * (0x00E576EA)
 *
 * Bead source-vy6m: the image never rejects a link in the root directory.
 * 0x00E57776-0x00E57788 only `seq`s the NAME_$ROOT_UID compare into a Domain
 * boolean at A6-0x136, which becomes dir_$old_add_link_entry's SEVENTH
 * argument at 0x00E577AE.  The tests below pin that, plus the shared
 * max-out-length cell 0x00E577F2 (DIR_$OLD_LINK_TEXT_MAX) and the
 * "nonzero unlock status wins" tail at 0x00E577DA.
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

name_$data_t NAME_$DATA;

/* 0xE577F2, the word 0x0100 shared with DIR_$OLD_READ_LINKU. */
int16_t DIR_$OLD_LINK_TEXT_MAX = 0x0100;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int8_t vl_result;
int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    (void)name; (void)name_len;
    parsed[0] = 'L';
    *parsed_len = 1;
    return vl_result;
}

static int      mc_calls;
static int16_t *mc_max_seen;
static int8_t   mc_truncated;
static int16_t  mc_out_len;
void MAP_CASE(char *name, int16_t *name_len, char *output,
              int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    mc_calls++;
    mc_max_seen = max_out_len;
    (void)name; (void)name_len;
    output[0] = 'X';
    *out_len = mc_out_len;
    *truncated = (uint8_t)mc_truncated;
}

static int      nv_calls;
static boolean  nv_result;
static uint16_t nv_len_seen;
boolean NAME_$VALIDATE(char *path, uint16_t *path_len, int16_t *consumed,
                       start_path_type_t *start_path_type)
{
    nv_calls++;
    nv_len_seen = *path_len;
    (void)path;
    *consumed = 1;
    *start_path_type = start_path_$relative;
    return nv_result;
}

static int       lock_calls;
static status_$t lock_status;
void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    lock_calls++;
    (void)dir_uid; (void)lock_mode; (void)acl_rights;
    *handle_ret = 0x00050000u;
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

static int       ale_calls;
static uint8_t   ale_flag_seen;
static uint16_t  ale_target_len_seen;
static uint16_t  ale_name_len_seen;
static status_$t ale_status;
void dir_$old_add_link_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                             uint16_t name_len, void *target,
                             uint16_t target_len, boolean is_root,
                             uint8_t *result, status_$t *status_ret)
{
    ale_calls++;
    ale_flag_seen = (uint8_t)is_root;
    ale_target_len_seen = target_len;
    ale_name_len_seen = name_len;
    (void)dir_uid; (void)handle; (void)name; (void)target;
    *(uint32_t *)result = 0;
    *status_ret = ale_status;
}

#include "../old_add_linku.c"

/* ------------------------------------------------------------------ */

static uid_t     dir;
static status_$t st;
static int16_t   name_len;
static uint16_t  target_len;
static char      the_name[] = "LNK";
static char      the_target[] = "//node/x";

static const uid_t ROOT = { 0x0BADF00Du, 0x0BADBEEFu };
static const uid_t OTHER = { 0x11111111u, 0x22222222u };

static void reset(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    NAME_$ROOT_UID = ROOT;
    dir = OTHER;
    vl_result = (int8_t)0xFF;
    mc_calls = 0; mc_max_seen = NULL; mc_truncated = 0; mc_out_len = 8;
    nv_calls = 0; nv_result = (boolean)0xFF; nv_len_seen = 0;
    lock_calls = 0; lock_status = status_$ok;
    unlock_calls = 0; unlock_status_out = status_$ok;
    exit_super_calls = 0;
    ale_calls = 0; ale_status = status_$ok; ale_flag_seen = 0xAA;
    ale_target_len_seen = 0xFFFF; ale_name_len_seen = 0xFFFF;
    st = 0x5A5A5A5A;
    name_len = 3;
    target_len = 8;
}

TEST(an_invalid_leaf_returns_before_map_case)
{
    reset();
    vl_result = 0;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(status_$naming_invalid_leaf, st);
    ASSERT_EQ(0, mc_calls);
}

/* 0x00E57732: `pea (0xbe,PC)` is the shared 0x0100 cell, not a local. */
TEST(map_case_gets_the_shared_length_cell)
{
    reset();
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(1, mc_calls);
    ASSERT_TRUE(mc_max_seen == &DIR_$OLD_LINK_TEXT_MAX);
    ASSERT_EQ(0x0100, *mc_max_seen);
}

/* 0x00E5774A: a truncated case-map is an invalid link. */
TEST(a_truncated_map_case_is_an_invalid_link)
{
    reset();
    mc_truncated = (int8_t)0xFF;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(status_$naming_invalid_link, st);
    ASSERT_EQ(0, nv_calls);
    ASSERT_EQ(0, lock_calls);
}

/* 0x00E5776A: so is a pathname NAME_$VALIDATE rejects. */
TEST(a_rejected_pathname_is_an_invalid_link)
{
    reset();
    nv_result = 0;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(status_$naming_invalid_link, st);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, exit_super_calls);
}

/* NAME_$VALIDATE gets MAP_CASE's output length, not the caller's. */
TEST(name_validate_sees_the_mapped_length)
{
    reset();
    mc_out_len = 42;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(42, nv_len_seen);
    ASSERT_EQ(42, ale_target_len_seen);
}

/*
 * 0x00E57776: a non-root directory yields the Domain boolean FALSE, and the
 * routine carries on - there is no early return.
 */
TEST(a_non_root_directory_passes_false_and_still_adds)
{
    reset();
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(1, ale_calls);
    ASSERT_EQ(0x00, ale_flag_seen);
    ASSERT_EQ(1, ale_name_len_seen);     /* the PARSED length */
    ASSERT_EQ(status_$ok, st);
}

/* The root directory yields TRUE (0xFF) - and is NOT rejected. */
TEST(the_root_directory_passes_true_and_is_not_rejected)
{
    reset();
    dir = ROOT;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(1, ale_calls);
    ASSERT_EQ(0xFF, ale_flag_seen);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E577A2: tst.l (A3) - the WHOLE longword gates the add. */
TEST(a_failed_lock_skips_the_add)
{
    reset();
    lock_status = status_$naming_directory_locked;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(0, ale_calls);
    ASSERT_EQ(0, unlock_calls);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E577DA: this tail replaces the status on a NONZERO unlock status. */
TEST(a_nonzero_unlock_status_replaces_the_bodys_status)
{
    reset();
    ale_status = status_$name_already_exists;
    unlock_status_out = status_$naming_internal_error;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(status_$naming_internal_error, st);
}

TEST(a_zero_unlock_status_leaves_the_status_alone)
{
    reset();
    ale_status = status_$name_already_exists;
    DIR_$OLD_ADD_LINKU(&dir, the_name, &name_len, the_target, &target_len, &st);
    ASSERT_EQ(status_$name_already_exists, st);
}

int main(void)
{
    printf("DIR_$OLD_ADD_LINKU (0x00E576EA) tests\n");

    RUN_TEST(an_invalid_leaf_returns_before_map_case);
    RUN_TEST(map_case_gets_the_shared_length_cell);
    RUN_TEST(a_truncated_map_case_is_an_invalid_link);
    RUN_TEST(a_rejected_pathname_is_an_invalid_link);
    RUN_TEST(name_validate_sees_the_mapped_length);
    RUN_TEST(a_non_root_directory_passes_false_and_still_adds);
    RUN_TEST(the_root_directory_passes_true_and_is_not_rejected);
    RUN_TEST(a_failed_lock_skips_the_add);
    RUN_TEST(a_nonzero_unlock_status_replaces_the_bodys_status);
    RUN_TEST(a_zero_unlock_status_leaves_the_status_alone);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
