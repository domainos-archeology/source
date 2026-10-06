/*
 * acl/test/test_set_def_acl_check.c - unit tests for ACL_$SET_DEF_ACL_CHECK
 * (0x00E48BF8)
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
uint16_t PROC1_$CURRENT = 6;

/* ---- mocks ---- */
static char log_buf[64];
static void note(const char *s) { strcat(log_buf, s); }
static status_$t image_status;
static uid_t image_owner;
static int16_t super_during;
static int8_t override_during;
static const void *image_uid_arg;

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; note("<"); }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e) { (void)e; note(">"); }
void ML_$LOCK(int16_t id) { if (id != 0xA) note("!"); note("L"); }
void ML_$UNLOCK(int16_t id) { if (id != 0xA) note("!"); note("U"); }
void acl_$image_internal(uid_t *source_uid, int16_t buffer_len, int8_t flag,
                         void *output_buf, int16_t *len_out,
                         acl_$prot_data_t *data_out, int8_t *flag_out,
                         status_$t *status)
{
    (void)data_out;
    note("I");
    image_uid_arg = source_uid;
    super_during = ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT];
    override_during = ACL_$UNWIRED_DATA.locksmith_override;
    if (buffer_len != 0x400 || flag != (int8_t)0xFF ||
        output_buf != ACL_$UNWIRED_DATA.workspace) { note("!"); }
    ((acl_$image_t *)output_buf)->acl_uid = image_owner;
    *len_out = 0x34;
    *flag_out = 0;
    *status = image_status;
}

#include "../set_def_acl_check.c"

static uid_t dir = { 0xD1, 0xD2 };
static uid_t acl = { 0x0200AA00, 0x55 };
static uid_t type = { 0x601, 0 };
static uint8_t prot[0x2C];
static const uint16_t four = 4;
static status_$t st;

static void reset_state(void)
{
    memset(&ACL_$UNWIRED_DATA, 0, sizeof(ACL_$UNWIRED_DATA));
    log_buf[0] = 0;
    image_status = 0;
    image_owner = type;
    image_uid_arg = NULL;
    st = 0x12345;
}

static void test_nil_acl_accepted_untouched(void)
{
    uid_t nil = { 0, 0 };
    ASSERT_EQ(0xFF, (uint8_t)ACL_$SET_DEF_ACL_CHECK(&dir, prot, &nil, &four, &type, &st));
    ASSERT_EQ(0, strlen(log_buf));
    ASSERT_EQ(0x12345, st);         /* status left as the caller had it */
}

static void test_matching_type_accepted(void)
{
    ASSERT_EQ(0xFF, (uint8_t)ACL_$SET_DEF_ACL_CHECK(&dir, prot, &acl, &four, &type, &st));
    ASSERT_EQ(0, strcmp(log_buf, "<LIU>"));
    ASSERT_EQ(0, st);
    ASSERT_EQ((unsigned long)&acl, (unsigned long)image_uid_arg);
    ASSERT_EQ(1, super_during);
    ASSERT_EQ(0xFF, (uint8_t)override_during);
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[6]);
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.locksmith_override);
    ASSERT_EQ(6, ACL_$UNWIRED_DATA.locksmith_owner_pid);
}

static void test_other_type_wrong_type(void)
{
    image_owner.high = 0x602;
    ASSERT_EQ(0, ACL_$SET_DEF_ACL_CHECK(&dir, prot, &acl, &four, &type, &st));
    ASSERT_EQ(status_$acl_wrong_type, st);
    ASSERT_EQ(0, strcmp(log_buf, "<LIU>"));
}

static void test_image_error_returned(void)
{
    image_status = 0x0023000D;
    ASSERT_EQ(0, ACL_$SET_DEF_ACL_CHECK(&dir, prot, &acl, &four, &type, &st));
    ASSERT_EQ(0x0023000D, st);
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[6]);
}

int main(void)
{
    printf("ACL_$SET_DEF_ACL_CHECK tests\n");
    RUN_TEST(nil_acl_accepted_untouched);
    RUN_TEST(matching_type_accepted);
    RUN_TEST(other_type_wrong_type);
    RUN_TEST(image_error_returned);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
