/*
 * acl/test/test_convert_to_10acl.c - unit tests for ACL_$CONVERT_TO_10ACL
 * (0x00E48E02)
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

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
MODULE_DATA_DEFINE(acl_$wired_data_t, ACL_$WIRED_DATA, 0x00E2C014);
uid_t UID_$NIL = { 0, 0 };
uid_t ACL_$FILE_ACL = { 0x00000602, 0 };
uid_t ACL_$DIR_ACL = { 0x00000601, 0 };
uint16_t PROC1_$CURRENT = 5;

/* ---- mocks ---- */
static char log_buf[64];
static void note(const char *s) { strcat(log_buf, s); }
static int16_t image_len_val;
static status_$t image_status, create_status;
static acl_$image_t image_val;
static int16_t create_len;
static const void *create_type, *create_dir;
static int16_t super_during;
static int8_t override_during;

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; note("<"); }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e) { (void)e; note(">"); }
void ML_$LOCK(int16_t id) { (void)id; note("L"); }
void ML_$UNLOCK(int16_t id) { (void)id; note("U"); }
void acl_$image_internal(uid_t *source_uid, int16_t buffer_len, int8_t flag,
                         void *output_buf, int16_t *len_out,
                         acl_$prot_data_t *data_out, int8_t *flag_out,
                         status_$t *status)
{
    (void)source_uid; (void)data_out; (void)flag_out;
    note("I");
    super_during = ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT];
    override_during = ACL_$UNWIRED_DATA.locksmith_override;
    if (buffer_len != 0x400 || flag != (int8_t)0xFF) { note("!"); }
    memcpy(output_buf, &image_val, sizeof(image_val));
    *(int16_t *)len_out = image_len_val;
    *status = image_status;
}
void ACL_$PRIM_CREATE(void *acl_data, int16_t *data_len, uid_t *dir_uid,
                      void *type, uid_t *file_uid_ret, status_$t *status_ret)
{
    (void)acl_data;
    note("C");
    create_len = *data_len; create_dir = dir_uid; create_type = type;
    file_uid_ret->high = 0xAC10; file_uid_ret->low = 1;
    *status_ret = create_status;
}

#include "../convert_to_10acl.c"

static uid_t src = { 0x1234, 0x5678 };
static uid_t dir = { 0xD1, 0xD2 };
static uint8_t data[0x2C];
static uid_t result;
static status_$t st;

static void reset_state(void)
{
    memset(&ACL_$UNWIRED_DATA, 0, sizeof(ACL_$UNWIRED_DATA));
    memset(&image_val, 0, sizeof(image_val));
    log_buf[0] = 0;
    image_len_val = 0x34;
    image_status = create_status = 0;
    image_val.acl_uid = ACL_$FILE_ACL;
    image_val.initial_acl_uid = ACL_$FILE_ACL;
    result.high = 0xFFFF; result.low = 0xFFFF;
    st = -1;
}

static void test_default_file_image_not_created(void)
{
    ACL_$CONVERT_TO_10ACL(&src, &dir, &result, data, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<LIU>"));
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, result.high);
    ASSERT_EQ(0, result.low);
    ASSERT_EQ(1, super_during);
    ASSERT_EQ(0xFF, (uint8_t)override_during);
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[5]);
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.locksmith_override);
    ASSERT_EQ(5, ACL_$UNWIRED_DATA.locksmith_owner_pid);
}

static void test_default_dir_image_not_created(void)
{
    image_val.acl_uid = ACL_$DIR_ACL;
    image_val.initial_acl_uid = ACL_$DIR_ACL;
    ACL_$CONVERT_TO_10ACL(&src, &dir, &result, data, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<LIU>"));
}

static void test_other_length_creates(void)
{
    image_len_val = 0x40;
    ACL_$CONVERT_TO_10ACL(&src, &dir, &result, data, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<LIUC>"));
    ASSERT_EQ(0x40, create_len);
    ASSERT_EQ((uintptr_t)&dir, (uintptr_t)create_dir);
    ASSERT_EQ((uintptr_t)data, (uintptr_t)create_type);
    ASSERT_EQ(0xAC10, result.high);
}

static void test_subsys_uid_creates(void)
{
    image_val.subsys_uid.high = 1;
    ACL_$CONVERT_TO_10ACL(&src, &dir, &result, data, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<LIUC>"));
}

static void test_initial_differs_creates(void)
{
    image_val.initial_acl_uid = ACL_$DIR_ACL;
    create_status = 0x00230007;
    ACL_$CONVERT_TO_10ACL(&src, &dir, &result, data, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<LIUC>"));
    ASSERT_EQ(0x00230007, st);
}

static void test_image_failure(void)
{
    image_status = 0x00230003;
    ACL_$CONVERT_TO_10ACL(&src, &dir, &result, data, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<LIU>"));
    ASSERT_EQ(0x00230003, st);
    ASSERT_EQ(0, result.high);          /* nil from the entry */
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[5]);
}

int main(void)
{
    printf("ACL_$CONVERT_TO_10ACL tests\n");
    RUN_TEST(default_file_image_not_created);
    RUN_TEST(default_dir_image_not_created);
    RUN_TEST(other_length_creates);
    RUN_TEST(subsys_uid_creates);
    RUN_TEST(initial_differs_creates);
    RUN_TEST(image_failure);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
