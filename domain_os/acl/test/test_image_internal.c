/*
 * acl/test/test_image_internal.c - unit tests for acl_$image_internal
 * (0x00E47B78)
 */

#include <stdio.h>
#include <stdint.h>
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

MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);
uid_t UID_$NIL = { 0, 0 };
uid_t ACL_$NIL = { 0x00000100, 0 };
uid_t ACL_$FILE_ACL = { 0x00000602, 0 };
uid_t ACL_$DIR_ACL = { 0x00000601, 0 };

/* ---- mocks ---- */
static char log_buf[64];
static void note(const char *s) { strcat(log_buf, s); }
static status_$t attr_status, rem_status, slot_status;
static uint8_t attr_flags[4];
static int8_t loc_remote;
static uid_t seen_uid;
static uint8_t seen_loc_flags;
static int16_t slot_ret;
static int8_t slot_flag;
static int16_t rem_version;
static uint8_t rem_type;

void AST_$GET_ACL_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                             ast_$acl_attr_t *acl, status_$t *status)
{
    note("A");
    if (flags != 1) note("!");
    seen_uid = loc_rec->uid;
    seen_loc_flags = (uint8_t)loc_rec->flags;
    loc_rec->flags = loc_remote;
    memcpy(acl->obj_flags, attr_flags, 4);
    *status = attr_status;
}
void REM_FILE_$ACL_IMAGE(void *addr_info, uid_t *file_uid, uint8_t acl_type,
                         void *acl_image_out, uint16_t *acl_len_out,
                         void *acl_header_out, status_$t *status)
{
    (void)addr_info; (void)acl_header_out;
    note("R");
    seen_uid = *file_uid;
    rem_type = acl_type;
    ((acl_$image_t *)acl_image_out)->version = (uint16_t)rem_version;
    *acl_len_out = 0x54;
    *status = rem_status;
}
int16_t acl_$find_acl_slot(uid_t *acl_uid, int8_t *cached_flag_ret,
                           acl_$prot_data_t *prot, status_$t *status_ret)
{
    (void)prot;
    note("S");
    seen_uid = *acl_uid;
    *cached_flag_ret = slot_flag;
    *status_ret = slot_status;
    return slot_ret;
}
void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out)
{
    note("D");
    memset(acl_data_out, 0x7F, 0x2C);
    memset(uid_out, 0, 8);
}

#include "../image_internal.c"

static uint8_t image[0x400];
static int16_t len;
static acl_$prot_data_t data;
static int8_t flag;
static status_$t st;
static uid_t src;

static void reset_state(void)
{
    memset(&ACL_$DATA, 0, sizeof(ACL_$DATA));
    memset(image, 0xEE, sizeof(image));
    memset(&data, 0x7F, sizeof(data));
    log_buf[0] = 0;
    attr_status = rem_status = slot_status = 0;
    attr_flags[0] = 0; attr_flags[1] = 3; attr_flags[2] = 0; attr_flags[3] = 0;
    loc_remote = 0;
    slot_ret = -1; slot_flag = 0; rem_version = 5;
    len = 0x7777; flag = 0x55; st = 0x5555;
    src.high = 0x12345678; src.low = 0x01000009;     /* bit 24 of low set */
}

static void test_nil_uid_default_file_image(void)
{
    src.high = 0; src.low = 0;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, strcmp(log_buf, "D"));
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x34, len);
    ASSERT_EQ(0, flag);
    ASSERT_EQ(5, ((acl_$image_t *)image)->version);
    ASSERT_EQ(0x602, ((acl_$image_t *)image)->acl_uid.high);
    ASSERT_EQ(0x602, ((acl_$image_t *)image)->initial_acl_uid.high);
    ASSERT_EQ(0, ((acl_$image_t *)image)->subsys_uid.high);
    ASSERT_EQ(0, ((acl_$image_t *)image)->rights[11]);
    ASSERT_EQ(0xEE, image[0x42]);                   /* only 0x42 bytes written */
}

static void test_local_object_cache_slot_copied(void)
{
    acl_$cache_slot_t *cs = &ACL_$DATA.acl_cache[3];
    cs->version = 5;
    cs->entry_count = 2;
    cs->entries[0x1B] = 0xC0;
    attr_flags[3] = 1;                               /* local */
    slot_ret = 3;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, strcmp(log_buf, "AS"));
    ASSERT_EQ(0x01000009 & ~0x01000000, seen_uid.low);
    ASSERT_EQ(0, seen_loc_flags & 0x40);
    ASSERT_EQ(0x74, len);
    ASSERT_EQ(0, st);
    ASSERT_EQ(5, ((acl_$cache_slot_t *)image)->version);
    ASSERT_EQ(0xC0, image[0x34 + 0x1B]);
    ASSERT_EQ(0xEE, image[0x74]);
}

static void test_cached_default_flag_clears_bit6(void)
{
    acl_$cache_slot_t *cs = &ACL_$DATA.acl_cache[1];
    cs->entry_count = 2;
    cs->entries[0x1B] = 0xC0;
    cs->entries[0x20 + 0x1B] = 0x41;
    slot_ret = 1; slot_flag = (int8_t)0xFF;
    attr_flags[3] = 1;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0xFF, (uint8_t)flag);
    ASSERT_EQ(0x80, image[0x34 + 0x1B]);
    ASSERT_EQ(0x01, image[0x54 + 0x1B]);
    ASSERT_EQ(0x3F, data.owner_rights);
    ASSERT_EQ(0x3F, data.subsys_rights);
    ASSERT_EQ(0x7F, data.reserved_1d[0]);
}

static void test_caller_flag_keeps_bit6(void)
{
    ACL_$DATA.acl_cache[1].entry_count = 1;
    slot_ret = 1; slot_flag = (int8_t)0xFF;
    attr_flags[3] = 1;
    acl_$image_internal(&src, 0x400, (int8_t)0xFF, image, &len, &data, &flag, &st);
    ASSERT_EQ(0x7F, data.owner_rights);
}

static void test_buffer_too_small(void)
{
    ACL_$DATA.acl_cache[2].entry_count = 4;
    slot_ret = 2;
    attr_flags[3] = 1;
    acl_$image_internal(&src, 0x93, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0xB4, len);
    ASSERT_EQ(status_$image_buffer_too_small, st);
    ASSERT_EQ(0xEE, image[0]);
}

static void test_slot_error_zero_length(void)
{
    slot_status = 0x0023000D;
    attr_flags[3] = 1;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, len);
    ASSERT_EQ(0x0023000D, st);
}

static void test_slot_none_dir_default(void)
{
    src.high = 0x00020000 | 0x01000000;   /* top byte non-zero, top word 0x0102 */
    attr_flags[3] = 1;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0x602, ((acl_$image_t *)image)->acl_uid.high);
    src.high = 0x00020000;                /* top byte zero: no AST call */
    log_buf[0] = 0;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, strcmp(log_buf, "S"));
    ASSERT_EQ(0x601, ((acl_$image_t *)image)->acl_uid.high);
}

static void test_acl_nil_skips_ast(void)
{
    src = ACL_$NIL;
    slot_ret = -1;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, strcmp(log_buf, "S"));
}

static void test_ast_error_only_status(void)
{
    attr_status = 0x00020005;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, strcmp(log_buf, "A"));
    ASSERT_EQ(0x00020005, st);
    ASSERT_EQ(0x7777, (uint16_t)len);
    ASSERT_EQ(0x55, flag);
}

static void test_not_an_acl_wrong_type(void)
{
    attr_flags[1] = 2;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(status_$acl_wrong_type, st);
    ASSERT_EQ(0x7777, (uint16_t)len);
}

static void test_remote_old_and_new_versions(void)
{
    loc_remote = (int8_t)0x80;
    rem_version = 3;
    acl_$image_internal(&src, 0x400, (int8_t)0xFF, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, strcmp(log_buf, "AR"));
    ASSERT_EQ(0xFF, rem_type);
    ASSERT_EQ(0x54, len);
    ASSERT_EQ(0xFF, (uint8_t)flag);
    rem_version = 5;
    acl_$image_internal(&src, 0x400, 0, image, &len, &data, &flag, &st);
    ASSERT_EQ(0, flag);
}

int main(void)
{
    printf("acl_$image_internal tests\n");
    RUN_TEST(nil_uid_default_file_image);
    RUN_TEST(local_object_cache_slot_copied);
    RUN_TEST(cached_default_flag_clears_bit6);
    RUN_TEST(caller_flag_keeps_bit6);
    RUN_TEST(buffer_too_small);
    RUN_TEST(slot_error_zero_length);
    RUN_TEST(slot_none_dir_default);
    RUN_TEST(acl_nil_skips_ast);
    RUN_TEST(ast_error_only_status);
    RUN_TEST(not_an_acl_wrong_type);
    RUN_TEST(remote_old_and_new_versions);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
