/*
 * dir/test/test_do_op_set_acl.c - Unit tests for dir_$do_op_set_acl
 *
 * Tests the server-side set-ACL DO_OP handler (0x00E52BC2) by mocking every
 * subsystem call it makes and verifying:
 *   - the super-mode bracket is balanced and spans the whole body
 *   - dir_$open_dir is called with mode 2 / rights 0
 *   - an open_dir failure skips the conversion but still releases the handle
 *     and still audits
 *   - funky-ACL selector bits of zero produce status_$acl_unimplemented_call
 *     without calling ACL_$CONVERT_FUNKY_ACL
 *   - the selector reads the HIGH word of the UID's low longword, masked
 *     0xFF0, shifted right 4, masked 0xE0
 *   - a conversion failure skips FILE_$SET_PROT
 *   - the success path calls FILE_$SET_PROT with protection type 4
 *   - the audit call is gated on AUDIT_$ENABLED being a NEGATIVE byte and
 *     names ACL_$DIRIN_ACL with prot_flags 4
 */

#include <stdio.h>
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

#include "dir/dir_internal.h"

/* ================================================================
 * Kernel globals the handler reads (real definitions live in the
 * image's data segment; the host build needs storage for them).
 * ================================================================ */

int8_t AUDIT_$ENABLED;
uid_t  ACL_$DIRIN_ACL;

/* ================================================================
 * Mock state
 * ================================================================ */

static int      mock_super_depth;
static int      mock_super_depth_at_set_prot;

static int      mock_open_dir_called;
static int16_t  mock_open_dir_mode;
static int16_t  mock_open_dir_rights;
static uid_t    mock_open_dir_uid;
static status_$t mock_open_dir_status;

static int      mock_release_called;

static int      mock_convert_called;
static status_$t mock_convert_status;
static uid_t    mock_convert_acl_uid_arg;

static int      mock_set_prot_called;
static uint16_t mock_set_prot_type;
static uid_t    mock_set_prot_file_uid;
static uint32_t mock_set_prot_acl_word0;

static int      mock_audit_called;
static status_$t mock_audit_status;
static uid_t    mock_audit_uid;
static uid_t    mock_audit_acl_uid;
static uint16_t mock_audit_flags;

/* The value ACL_$CONVERT_FUNKY_ACL writes into acl_data[0] on success. */
#define CONVERTED_ACL_WORD0 0xFEEDFACEUL

static void reset_mocks(void)
{
    mock_super_depth = 0;
    mock_super_depth_at_set_prot = -1;

    mock_open_dir_called = 0;
    mock_open_dir_mode = -1;
    mock_open_dir_rights = -1;
    mock_open_dir_uid = (uid_t){0, 0};
    mock_open_dir_status = status_$ok;

    mock_release_called = 0;

    mock_convert_called = 0;
    mock_convert_status = status_$ok;
    mock_convert_acl_uid_arg = (uid_t){0, 0};

    mock_set_prot_called = 0;
    mock_set_prot_type = 0xFFFF;
    mock_set_prot_file_uid = (uid_t){0, 0};
    mock_set_prot_acl_word0 = 0;

    mock_audit_called = 0;
    mock_audit_status = 0xFFFFFFFFUL;
    mock_audit_uid = (uid_t){0, 0};
    mock_audit_acl_uid = (uid_t){0, 0};
    mock_audit_flags = 0xFFFF;

    AUDIT_$ENABLED = 0;
    ACL_$DIRIN_ACL = (uid_t){0x00000603UL, 0};  /* 0xE1745C */
}

/* ================================================================
 * Mock function implementations
 * ================================================================ */

void ACL_$ENTER_SUPER(void)
{
    mock_super_depth++;
}

void ACL_$EXIT_SUPER(void)
{
    mock_super_depth--;
}

void dir_$open_dir(void *uid, int16_t mode, int16_t rights,
                   void *handle_ret, status_$t *status_ret)
{
    mock_open_dir_called = 1;
    mock_open_dir_mode = mode;
    mock_open_dir_rights = rights;
    mock_open_dir_uid = *(uid_t *)uid;
    *(uint32_t *)handle_ret = 0x1234;
    *status_ret = mock_open_dir_status;
}

void dir_$release_handle(void *handle_ptr)
{
    (void)handle_ptr;
    mock_release_called = 1;
}

void ACL_$CONVERT_FUNKY_ACL(void *acl_uid, void *acl_data_out,
                            void *prot_info_out, void *target_uid_out,
                            status_$t *status_ret)
{
    mock_convert_called = 1;
    mock_convert_acl_uid_arg = *(uid_t *)acl_uid;
    *status_ret = mock_convert_status;
    if (mock_convert_status == status_$ok) {
        ((uint32_t *)acl_data_out)[0] = CONVERTED_ACL_WORD0;
        *(uid_t *)prot_info_out = (uid_t){0x0BADF00DUL, 0x0000000AUL};
        *(uid_t *)target_uid_out = (uid_t){0x00000601UL, 0};
    }
}

void FILE_$SET_PROT(uid_t *file_uid, uint16_t *prot_type, void *acl_data,
                    uid_t *acl_uid, status_$t *status_ret)
{
    (void)acl_uid;
    mock_set_prot_called = 1;
    mock_set_prot_type = *prot_type;
    mock_set_prot_file_uid = *file_uid;
    mock_set_prot_acl_word0 = ((uint32_t *)acl_data)[0];
    mock_super_depth_at_set_prot = mock_super_depth;
    *status_ret = status_$ok;
}

void audit_$log_prot_op(status_$t status, uid_t *uid, void *prot_data,
                        uid_t *acl_uid, uid_t *subject_uid, uint16_t prot_flags)
{
    (void)prot_data;
    (void)subject_uid;
    mock_audit_called = 1;
    mock_audit_status = status;
    mock_audit_uid = *uid;
    mock_audit_acl_uid = *acl_uid;
    mock_audit_flags = prot_flags;
}

/* ================================================================
 * Code under test
 * ================================================================ */

#include "../do_op_set_acl.c"

/* A funky UID whose selector bits (high word of .low, & 0xFF0, >>4, & 0xE0)
 * are non-zero: 0x0800 >> 4 = 0x80. */
#define FUNKY_LOW_OK   0x08000000UL
/* Selector bits zero: 0x0100 >> 4 = 0x10, & 0xE0 == 0. */
#define FUNKY_LOW_ZERO 0x01000000UL

/* ================================================================
 * Tests
 * ================================================================ */

TEST(success_path_calls_set_prot_with_type_4)
{
    reset_mocks();

    uid_t dir_uid = {0xAAAA1111UL, 0xBBBB2222UL};
    uid_t acl_uid = {0x12345678UL, FUNKY_LOW_OK};
    status_$t status = 0xDEADBEEFUL;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(1, mock_open_dir_called);
    ASSERT_EQ(2, mock_open_dir_mode);
    ASSERT_EQ(0, mock_open_dir_rights);
    ASSERT_EQ(1, mock_convert_called);
    ASSERT_EQ(1, mock_set_prot_called);
    ASSERT_EQ(4, mock_set_prot_type);
    ASSERT_EQ(CONVERTED_ACL_WORD0, mock_set_prot_acl_word0);
    ASSERT_EQ(dir_uid.high, mock_set_prot_file_uid.high);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_release_called);
    /* Super-mode bracket balanced, and FILE_$SET_PROT ran inside it. */
    ASSERT_EQ(0, mock_super_depth);
    ASSERT_EQ(1, mock_super_depth_at_set_prot);
    /* Auditing disabled -> no audit record. */
    ASSERT_EQ(0, mock_audit_called);
}

TEST(open_dir_failure_skips_conversion_but_releases)
{
    reset_mocks();
    mock_open_dir_status = 0x00120005UL;

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, FUNKY_LOW_OK};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(0x00120005UL, status);
    ASSERT_EQ(0, mock_convert_called);
    ASSERT_EQ(0, mock_set_prot_called);
    ASSERT_EQ(1, mock_release_called);
    ASSERT_EQ(0, mock_super_depth);
}

TEST(zero_selector_bits_is_unimplemented_call)
{
    reset_mocks();

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, FUNKY_LOW_ZERO};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(0x0023001CUL, status);
    ASSERT_EQ(0, mock_convert_called);
    ASSERT_EQ(0, mock_set_prot_called);
    ASSERT_EQ(1, mock_release_called);
}

TEST(selector_reads_high_word_of_low_longword)
{
    /* The low longword's LOW word carrying 0x0800 must NOT satisfy the test:
     * the machine code reads (0x4,A3), the high word. */
    reset_mocks();

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, 0x00000800UL};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(0x0023001CUL, status);
    ASSERT_EQ(0, mock_convert_called);
}

TEST(selector_bit_0x20_is_accepted)
{
    reset_mocks();

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, 0x02000000UL};   /* 0x0200 >> 4 = 0x20 */
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(1, mock_convert_called);
    ASSERT_EQ(1, mock_set_prot_called);
}

TEST(selector_bits_above_0xff0_are_masked_off)
{
    /* 0x1000 in the high word is outside the 0xFF0 mask, so the selector is
     * zero even though the raw word is large. */
    reset_mocks();

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, 0x10000000UL};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(0x0023001CUL, status);
    ASSERT_EQ(0, mock_convert_called);
}

TEST(convert_failure_skips_set_prot)
{
    reset_mocks();
    mock_convert_status = 0x00230003UL;

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, FUNKY_LOW_OK};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(0x00230003UL, status);
    ASSERT_EQ(1, mock_convert_called);
    ASSERT_EQ(0, mock_set_prot_called);
    ASSERT_EQ(1, mock_release_called);
    ASSERT_EQ(0, mock_super_depth);
}

TEST(convert_receives_the_caller_supplied_acl_uid)
{
    reset_mocks();

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0x5A5A5A5AUL, FUNKY_LOW_OK};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(0x5A5A5A5AUL, mock_convert_acl_uid_arg.high);
    ASSERT_EQ(FUNKY_LOW_OK, mock_convert_acl_uid_arg.low);
}

TEST(audit_runs_when_enabled_byte_is_negative)
{
    reset_mocks();
    AUDIT_$ENABLED = (int8_t)0xFF;

    uid_t dir_uid = {0xC0FFEEUL, 0x0BUL};
    uid_t acl_uid = {0, FUNKY_LOW_OK};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(1, mock_audit_called);
    ASSERT_EQ(status_$ok, mock_audit_status);
    ASSERT_EQ(0xC0FFEEUL, mock_audit_uid.high);
    ASSERT_EQ(ACL_$DIRIN_ACL.high, mock_audit_acl_uid.high);
    ASSERT_EQ(4, mock_audit_flags);
}

TEST(audit_skipped_when_enabled_byte_is_positive)
{
    /* A Domain BOOLEAN is tested SIGNED: 0x7F is "true-ish" as an unsigned
     * byte but must NOT trigger the audit. */
    reset_mocks();
    AUDIT_$ENABLED = 0x7F;

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, FUNKY_LOW_OK};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(0, mock_audit_called);
}

TEST(audit_reports_the_failure_status)
{
    /* The audit call is outside every branch: it happens even when open_dir
     * failed and nothing was converted. */
    reset_mocks();
    AUDIT_$ENABLED = (int8_t)0x80;
    mock_open_dir_status = 0x00120005UL;

    uid_t dir_uid = {7, 8};
    uid_t acl_uid = {0, FUNKY_LOW_OK};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(1, mock_audit_called);
    ASSERT_EQ(0x00120005UL, mock_audit_status);
}

TEST(unimplemented_status_is_audited_too)
{
    reset_mocks();
    AUDIT_$ENABLED = (int8_t)0xFF;

    uid_t dir_uid = {1, 2};
    uid_t acl_uid = {0, FUNKY_LOW_ZERO};
    status_$t status = status_$ok;

    dir_$do_op_set_acl(&dir_uid, &acl_uid, &status);

    ASSERT_EQ(1, mock_audit_called);
    ASSERT_EQ(0x0023001CUL, mock_audit_status);
}

/* ================================================================
 * Test runner
 * ================================================================ */

int main(void)
{
    printf("=== dir_$do_op_set_acl tests ===\n");

    RUN_TEST(success_path_calls_set_prot_with_type_4);
    RUN_TEST(open_dir_failure_skips_conversion_but_releases);
    RUN_TEST(zero_selector_bits_is_unimplemented_call);
    RUN_TEST(selector_reads_high_word_of_low_longword);
    RUN_TEST(selector_bit_0x20_is_accepted);
    RUN_TEST(selector_bits_above_0xff0_are_masked_off);
    RUN_TEST(convert_failure_skips_set_prot);
    RUN_TEST(convert_receives_the_caller_supplied_acl_uid);
    RUN_TEST(audit_runs_when_enabled_byte_is_negative);
    RUN_TEST(audit_skipped_when_enabled_byte_is_positive);
    RUN_TEST(audit_reports_the_failure_status);
    RUN_TEST(unimplemented_status_is_audited_too);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
