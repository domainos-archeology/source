/*
 * dir/test/test_do_op_add_link.c - Unit tests for dir_$do_op_add_link
 *
 * Tests the add entry/hard link DO_OP handler by mocking all subsystem
 * calls (add_entry, drop_entry, AST, ACL) and verifying:
 *   - Success path: add_entry + AST attrs + SET_ATTRIBUTE
 *   - Hard link rights checking (ACL_$RIGHTS with mask 0x48)
 *   - Too-many-hard-links limit (link_count >= 0xFFF5)
 *   - Object-not-found handling (regular add vs hard link)
 *   - Failure rollback via drop_entry
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Condition false at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ================================================================
 * Types under test
 * ================================================================ */

#include "dir/dir_internal.h"

/*
 * The result-buffer offsets are dir/do_op_add_link.c's own; they are repeated
 * here (identically, so the redefinitions are legal) because the mocks below
 * are written before the implementation is pulled in.
 */
#define ADDRES_ENTRY_TYPE       0x00
#define ADDRES_COMMON_ATTRS     0x08
#define ADDRES_REFCOUNT         0x1C
#define ADDRES_TARGET_UID       0x20
#define ADDRES_OBJ_TYPE         0x22
#define ADDRES_FILE_UID_COPY    0x28
#define ADDRES_FLAGS            0x3D
#define ADDRES_DROP_BUF         0x40
#define ADDRES_TOTAL_SIZE       0x48

/* ================================================================
 * Mock state
 * ================================================================ */

/* dir_$do_op_add_entry mock state */
static int mock_add_entry_called;
static status_$t mock_add_entry_status;
static int16_t mock_add_entry_type;       /* entry type to write at result[0] */
static uid_t mock_add_entry_target_uid;   /* UID to write at result[0x20] */
static uint8_t mock_add_entry_flags;      /* flags byte at result[0x3D] */

/* AST_$GET_COMMON_ATTRIBUTES mock state */
static int mock_get_common_attrs_called;
static status_$t mock_get_common_attrs_status;
static uint16_t mock_get_common_attrs_refcount;  /* at output[0x14] */
static int16_t mock_get_common_attrs_obj_type;     /* at output[0x1A] */

/* AST_$SET_ATTRIBUTE mock state */
static int mock_set_attr_called;
static uint16_t mock_set_attr_id;
static uint16_t mock_set_attr_value;
static status_$t mock_set_attr_status;

/* ACL_$RIGHTS mock state */
static int mock_acl_rights_called;
static uint32_t mock_acl_rights_return;
static boolean mock_acl_ignore_super;
static uint32_t mock_acl_rights_mask;
static int16_t mock_acl_option_flags;
static status_$t mock_acl_rights_status;

/* NAME_CONVERT_ACL_STATUS mock state */
static int mock_convert_acl_called;
static status_$t mock_convert_acl_output;

/* dir_$do_op_drop_entry mock state */
static int mock_drop_entry_called;
static uint16_t mock_drop_entry_rights;
static uint16_t mock_drop_entry_type;

/* dir_$find_entry - just a function pointer, never actually called */
char dir_$find_entry(void *h, void *n, int16_t nl, int16_t f,
                     void **e, void *x, int16_t *d)
{
    return 0;
}

static void reset_mocks(void)
{
    mock_add_entry_called = 0;
    mock_add_entry_status = status_$ok;
    mock_add_entry_type = 2;
    mock_add_entry_target_uid = (uid_t){0x11111111, 0x22222222};
    mock_add_entry_flags = 0x40;  /* bit 6 set, will be cleared */

    mock_get_common_attrs_called = 0;
    mock_get_common_attrs_status = status_$ok;
    mock_get_common_attrs_refcount = 0;
    mock_get_common_attrs_obj_type = 2;

    mock_set_attr_called = 0;
    mock_set_attr_id = 0;
    mock_set_attr_value = 0;
    mock_set_attr_status = status_$ok;

    mock_acl_rights_called = 0;
    mock_acl_rights_return = 0x48;  /* both modify and link rights */
    mock_acl_rights_status = status_$ok;

    mock_convert_acl_called = 0;
    mock_convert_acl_output = 0x000E0099;  /* some naming error */

    mock_drop_entry_called = 0;
    mock_drop_entry_rights = 0xFFFF;
    mock_drop_entry_type = 0xFFFF;
}

/* ================================================================
 * Mock function implementations
 * ================================================================ */

void dir_$do_op_add_entry(uid_t *uid_arg, int16_t type, void *name,
                          uint16_t name_len, uint16_t entry_type,
                          uint32_t extra, uid_t *uid_data,
                          uint16_t target_len, uint32_t target_data,
                          void *result, status_$t *status_ret)
{
    mock_add_entry_called = 1;
    *status_ret = mock_add_entry_status;

    if (mock_add_entry_status == status_$ok) {
        uint8_t *buf = (uint8_t *)result;
        memset(buf, 0, ADDRES_TOTAL_SIZE);

        /* Write entry type at offset 0x00 */
        *(int16_t *)(buf + ADDRES_ENTRY_TYPE) = mock_add_entry_type;

        /* Write target UID at offset 0x20 */
        *(uid_t *)(buf + ADDRES_TARGET_UID) = mock_add_entry_target_uid;

        /* Write flags byte at offset 0x3D */
        buf[ADDRES_FLAGS] = mock_add_entry_flags;
    }
}

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_arg, uint16_t flags_arg,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    mock_get_common_attrs_called = 1;
    *status = mock_get_common_attrs_status;

    if (mock_get_common_attrs_status == status_$ok) {
        uint8_t *buf = (uint8_t *)(void *)attrs;
        /* The reference count is the record's own +0x14 field. */
        attrs->refcount = mock_get_common_attrs_refcount;
        /*
         * Result offset 0x22 is the *descriptor's* +0x02 word, which
         * AST_$GET_ATTRIBUTES fills from aote+0x9E - it lies two bytes past
         * the end of the 0x18-byte record, in the frame area the real
         * dir_$do_op_add_link shares between the two (record at A6-0x78,
         * descriptor at A6-0x60).
         */
        *(int16_t *)(void *)(buf + (ADDRES_OBJ_TYPE - ADDRES_COMMON_ATTRS)) =
            mock_get_common_attrs_obj_type;
    }
}

void AST_$SET_ATTRIBUTE(uid_t *uid_arg, uint16_t attr_id,
                        void *value, status_$t *status)
{
    mock_set_attr_called = 1;
    mock_set_attr_id = attr_id;
    mock_set_attr_value = *(uint16_t *)value;
    *status = mock_set_attr_status;
}

uint32_t ACL_$RIGHTS(uid_t *uid_arg, boolean *ignore_super,
                     uint32_t *required_mask, int16_t *option_flags,
                     status_$t *status)
{
    (void)uid_arg;
    mock_acl_rights_called = 1;
    mock_acl_ignore_super = *ignore_super;
    mock_acl_rights_mask  = *required_mask;
    mock_acl_option_flags = *option_flags;
    *status = mock_acl_rights_status;
    return mock_acl_rights_return;
}

void NAME_CONVERT_ACL_STATUS(status_$t *status_ret)
{
    mock_convert_acl_called = 1;
    *status_ret = mock_convert_acl_output;
}

void dir_$do_op_drop_entry(uid_t *uid_arg, uint16_t rights,
                           void *name, uint16_t name_len,
                           uint16_t entry_type, void *result_uid,
                           status_$t *status_ret)
{
    mock_drop_entry_called = 1;
    mock_drop_entry_rights = rights;
    mock_drop_entry_type = entry_type;
    *status_ret = status_$ok;
}

/* ================================================================
 * Function under test - include directly
 * ================================================================ */

/* Pull in the actual implementation */
#include "../do_op_add_link.c"

/* ================================================================
 * Tests
 * ================================================================ */

TEST(add_entry_failure_returns_immediately)
{
    reset_mocks();
    mock_add_entry_status = status_$name_already_exists;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "testfile";

    dir_$do_op_add_link(&dir_uid, name, 8, &file_uid, 0, &status);

    ASSERT_EQ(status_$name_already_exists, status);
    ASSERT_EQ(1, mock_add_entry_called);
    ASSERT_EQ(0, mock_get_common_attrs_called);
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(0, mock_drop_entry_called);
}

TEST(success_zero_link_count_sets_attribute)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 0;
    mock_get_common_attrs_obj_type = 2;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "newfile";

    dir_$do_op_add_link(&dir_uid, name, 7, &file_uid, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_add_entry_called);
    ASSERT_EQ(1, mock_get_common_attrs_called);
    ASSERT_EQ(1, mock_set_attr_called);
    ASSERT_EQ(6, mock_set_attr_id);
    ASSERT_EQ(1, mock_set_attr_value);
    ASSERT_EQ(0, mock_acl_rights_called);
    ASSERT_EQ(0, mock_drop_entry_called);
}

TEST(nonzero_link_count_checks_acl_then_sets_attribute)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 5;
    mock_get_common_attrs_obj_type = 2;
    mock_acl_rights_return = 0x48;  /* has both modify and link */
    mock_acl_rights_status = status_$ok;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "hardlink";

    dir_$do_op_add_link(&dir_uid, name, 8, &file_uid, true, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_acl_rights_called);
    ASSERT_EQ(1, mock_set_attr_called);
    ASSERT_EQ(6, mock_set_attr_id);
    ASSERT_EQ(1, mock_set_attr_value);
    ASSERT_EQ(0, mock_drop_entry_called);
}

TEST(acl_modify_but_no_link_returns_insufficient_rights)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 5;
    mock_get_common_attrs_obj_type = 2;
    mock_acl_rights_return = 0x40;  /* modify but NOT link */
    mock_acl_rights_status = status_$ok;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "badlink";

    dir_$do_op_add_link(&dir_uid, name, 7, &file_uid, true, &status);

    ASSERT_EQ(status_$naming_insufficient_rights, status);
    ASSERT_EQ(1, mock_acl_rights_called);
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(1, mock_drop_entry_called);
    ASSERT_EQ(0, mock_drop_entry_rights);
    ASSERT_EQ(2, mock_drop_entry_type);
}

TEST(too_many_hard_links)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 0xFFF5;  /* at the limit */
    mock_get_common_attrs_obj_type = 2;
    mock_acl_rights_return = 0x48;  /* has both rights */
    mock_acl_rights_status = status_$ok;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "toomany";

    dir_$do_op_add_link(&dir_uid, name, 7, &file_uid, true, &status);

    ASSERT_EQ(status_$naming_too_many_hard_links, status);
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(1, mock_drop_entry_called);
}

TEST(link_count_just_below_limit_succeeds)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 0xFFF4;  /* one below limit */
    mock_get_common_attrs_obj_type = 2;
    mock_acl_rights_return = 0x48;
    mock_acl_rights_status = status_$ok;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "oklink";

    dir_$do_op_add_link(&dir_uid, name, 6, &file_uid, true, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_set_attr_called);
    ASSERT_EQ(0, mock_drop_entry_called);
}

TEST(object_not_found_regular_add_continues)
{
    reset_mocks();
    mock_get_common_attrs_status = file_$object_not_found;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "notfound";

    /* flags = 0 → regular add (non-negative) */
    dir_$do_op_add_link(&dir_uid, name, 8, &file_uid, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_get_common_attrs_called);
    /* Object not found → bit 7 set → skip validation → status stays OK */
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(0, mock_acl_rights_called);
    ASSERT_EQ(0, mock_drop_entry_called);
}

TEST(object_not_found_hard_link_fails)
{
    reset_mocks();
    mock_get_common_attrs_status = file_$object_not_found;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "notfound";

    /* The 5th argument is a Domain boolean BYTE (`move.b (0x16,A6),D2b`
     * at 0x00E5045E); 0xFF selects the hard-link path. */
    dir_$do_op_add_link(&dir_uid, name, 8, &file_uid, true, &status);

    ASSERT_EQ(file_$object_not_found, status);
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(1, mock_drop_entry_called);
}

TEST(entry_type_mismatch_skips_attribute_set)
{
    reset_mocks();
    mock_add_entry_type = 2;             /* entry type from add */
    mock_get_common_attrs_obj_type = 3;  /* different object type from attrs */
    mock_get_common_attrs_refcount = 0;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "mismatch";

    dir_$do_op_add_link(&dir_uid, name, 8, &file_uid, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(0, mock_acl_rights_called);
    ASSERT_EQ(0, mock_drop_entry_called);
}

TEST(acl_other_error_converts_status)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 5;
    mock_get_common_attrs_obj_type = 2;
    mock_acl_rights_status = 0x00230099;  /* some other ACL error */
    mock_convert_acl_output = 0x000E0099; /* converted naming error */

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "aclerr";

    dir_$do_op_add_link(&dir_uid, name, 6, &file_uid, true, &status);

    ASSERT_EQ(0x000E0099, status);
    ASSERT_EQ(1, mock_convert_acl_called);
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(1, mock_drop_entry_called);
}

TEST(set_attribute_failure_calls_drop_entry)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 0;
    mock_get_common_attrs_obj_type = 2;
    mock_set_attr_status = 0x000F9999;  /* some AST error */

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "setfail";

    dir_$do_op_add_link(&dir_uid, name, 7, &file_uid, 0, &status);

    ASSERT_EQ(0x000F9999, status);
    ASSERT_EQ(1, mock_set_attr_called);
    ASSERT_EQ(1, mock_drop_entry_called);
}

TEST(insufficient_rights_status_checks_rights_mask)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 5;
    mock_get_common_attrs_obj_type = 2;
    mock_acl_rights_return = 0x48;  /* has both rights */
    mock_acl_rights_status = status_$insufficient_rights_to_perform_operation;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "insuff";

    dir_$do_op_add_link(&dir_uid, name, 6, &file_uid, true, &status);

    /* Has both rights bits → proceed to link count check → succeeds */
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_set_attr_called);
}

TEST(no_right_status_with_modify_only_returns_naming_error)
{
    reset_mocks();
    mock_get_common_attrs_refcount = 5;
    mock_get_common_attrs_obj_type = 2;
    mock_acl_rights_return = 0x40;  /* modify only, no link */
    mock_acl_rights_status = status_$no_right_to_perform_operation;

    uid_t dir_uid = {0xAAAA, 0xBBBB};
    uid_t file_uid = {0xCCCC, 0xDDDD};
    status_$t status;
    char name[] = "noright";

    dir_$do_op_add_link(&dir_uid, name, 7, &file_uid, true, &status);

    ASSERT_EQ(status_$naming_insufficient_rights, status);
    ASSERT_EQ(0, mock_set_attr_called);
    ASSERT_EQ(1, mock_drop_entry_called);
}

/* ================================================================
 * Test runner
 * ================================================================ */

int main(void)
{
    printf("=== dir_$do_op_add_link tests ===\n");

    RUN_TEST(add_entry_failure_returns_immediately);
    RUN_TEST(success_zero_link_count_sets_attribute);
    RUN_TEST(nonzero_link_count_checks_acl_then_sets_attribute);
    RUN_TEST(acl_modify_but_no_link_returns_insufficient_rights);
    RUN_TEST(too_many_hard_links);
    RUN_TEST(link_count_just_below_limit_succeeds);
    RUN_TEST(object_not_found_regular_add_continues);
    RUN_TEST(object_not_found_hard_link_fails);
    RUN_TEST(entry_type_mismatch_skips_attribute_set);
    RUN_TEST(acl_other_error_converts_status);
    RUN_TEST(set_attribute_failure_calls_drop_entry);
    RUN_TEST(insufficient_rights_status_checks_rights_mask);
    RUN_TEST(no_right_status_with_modify_only_returns_naming_error);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
