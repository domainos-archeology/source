/*
 * file/test/test_set_prot.c - FILE_$SET_PROT (0x00E5DF3A) and
 * FILE_$SET_PROT_INT (0x00E5DD08) protection-selector and override logic.
 *
 * The two regressions this file exists for:
 *
 *  source-kzmp  0x00E5DF52-0x00E5DF62 reads `(0x4,A2)` as a WORD, which on
 *               this big-endian target is the HIGH word of acl_uid->low; the
 *               #0x0FF0 mask and `lsr.w #4` then leave (low >> 20) & 0xFF and
 *               `btst #4` tests BIT 24 of low.  The tree tested bit 8.
 *
 *  source-w7lk  0x00E5DE6A-0x00E5DE9A clears the 0x230010 status when
 *               ACL_$GET_LOCAL_LOCKSMITH() == 0 OR PROC1_$TYPE[cur] != 9 -
 *               the `beq 0x00E5DE9C` at 0x00E5DE98 SKIPS the clear when the
 *               type IS 9.  The tree had that disjunct inverted.  The same
 *               bead also retyped subsys_flag: 0x00E5DD1E reads it with
 *               `move.b (0x14,A6),D3b`, the high half of the word slot.
 *
 * The real file/set_prot.c and file/set_prot_int.c are #included at the
 * bottom; everything they call is mocked here.
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
    printf("  Running %-48s ", #name);          \
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

/* ==========================================================================
 * Globals the code under test reaches
 * ========================================================================== */

uint16_t PROC1_$CURRENT = 3;
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];
int8_t   AUDIT_$ENABLED = 0;

/* ==========================================================================
 * Mock bookkeeping
 * ========================================================================== */

static int      mock_get_common_calls;
static uint8_t  mock_obj_type;
static status_$t mock_get_common_status;

static int      mock_def_acldata_calls;

static int       mock_prot_int_calls;
static uint16_t  mock_prot_int_attr;
static uint16_t  mock_prot_int_type;
static boolean   mock_prot_int_subsys;
static uint32_t  mock_prot_int_acl0;
static uint32_t  mock_prot_int_acl1;

static int      mock_audit_calls;

/* SET_PROT_INT's own callees */
static int8_t    mock_same_volume_result;
static status_$t mock_same_volume_status;
static int       mock_same_volume_calls;

static int8_t    mock_loc_flags;
static status_$t mock_get_location_status;

static boolean   mock_acl_check_result;
static boolean   mock_acl_check_setid;
static status_$t mock_acl_check_status;
static int       mock_acl_check_calls;

static int16_t   mock_locksmith_result;
static int       mock_locksmith_calls;

static int       mock_shutwired_calls;
static int       mock_set_attribute_calls;
static status_$t mock_set_attribute_status;

/* ==========================================================================
 * Mocks
 * ========================================================================== */

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    (void)loc_rec; (void)flags;
    mock_get_common_calls++;
    memset(attrs, 0, sizeof(*attrs));
    attrs->obj_type = mock_obj_type;
    *status = mock_get_common_status;
}

void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out)
{
    mock_def_acldata_calls++;
    memset(acl_data_out, 0xCC, 44);
    memset(uid_out, 0xDD, 8);
}

void FILE_$AUDIT_SET_PROT(uid_t *file_uid, void *acl_data, void *prot_info,
                          uint16_t prot_type, status_$t status)
{
    (void)file_uid; (void)acl_data; (void)prot_info; (void)prot_type;
    (void)status;
    mock_audit_calls++;
}

int8_t FILE_$CHECK_SAME_VOLUME(uid_t *file_uid1, uid_t *file_uid2,
                               int8_t copy_location, uint32_t *location_out,
                               status_$t *status_ret)
{
    (void)file_uid1; (void)file_uid2; (void)copy_location; (void)location_out;
    mock_same_volume_calls++;
    *status_ret = mock_same_volume_status;
    return mock_same_volume_result;
}

void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *location_out,
                       status_$t *status)
{
    (void)flags; (void)unused; (void)location_out;
    loc_rec->flags = mock_loc_flags;
    *status = mock_get_location_status;
}

boolean ACL_$SET_ACL_CHECK(uid_t *obj_uid, acl_$prot_data_t *new_prot,
                           uid_t *acl_uid, int16_t *op_type,
                           boolean *setid_ret, status_$t *status_ret)
{
    (void)obj_uid; (void)new_prot; (void)acl_uid; (void)op_type;
    mock_acl_check_calls++;
    *setid_ret = mock_acl_check_setid;
    *status_ret = mock_acl_check_status;
    return mock_acl_check_result;
}

int16_t ACL_$GET_LOCAL_LOCKSMITH(void)
{
    mock_locksmith_calls++;
    return mock_locksmith_result;
}

void OS_PROC_SHUTWIRED(status_$t *status)
{
    (void)status;
    mock_shutwired_calls++;
}

void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status)
{
    (void)uid; (void)attr_id; (void)value;
    mock_set_attribute_calls++;
    *status = mock_set_attribute_status;
}

void ACL_$GET_EXSID(void *exsid, status_$t *status)
{
    memset(exsid, 0, 104);
    *status = status_$ok;
}

void REM_FILE_$FILE_SET_PROT(void *addr_info, uid_t *file_uid,
                             void *prot_data1, uint16_t flags,
                             void *prot_data2, uint8_t flag,
                             clock_t *mtime_out, status_$t *status)
{
    (void)addr_info; (void)file_uid; (void)prot_data1; (void)flags;
    (void)prot_data2; (void)flag; (void)mtime_out;
    *status = status_$ok;
}

void AST_$SET_ATTR(uid_t *uid, int16_t attr_id, void *value, uint8_t flags,
                   clock_t *clock, status_$t *status)
{
    (void)uid; (void)attr_id; (void)value; (void)flags; (void)clock;
    *status = status_$ok;
}

/* ==========================================================================
 * FILE_$SET_PROT_INT is mocked for the FILE_$SET_PROT tests and real for its
 * own; the real one is compiled under a second name so both can coexist.
 * ========================================================================== */

#define FILE_$SET_PROT_INT  real_FILE_$SET_PROT_INT
#include "../set_prot_int.c"
#undef FILE_$SET_PROT_INT

void FILE_$SET_PROT_INT(uid_t *file_uid, void *acl_data, uint16_t attr_type,
                        uint16_t prot_type, boolean subsys_flag,
                        status_$t *status_ret)
{
    (void)file_uid;
    mock_prot_int_calls++;
    mock_prot_int_attr   = attr_type;
    mock_prot_int_type   = prot_type;
    mock_prot_int_subsys = subsys_flag;
    mock_prot_int_acl0   = ((uint32_t *)acl_data)[0];
    mock_prot_int_acl1   = ((uint32_t *)acl_data)[1];
    *status_ret = status_$ok;
}

#include "../set_prot.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void reset(void)
{
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));
    PROC1_$CURRENT = 3;
    AUDIT_$ENABLED = 0;

    mock_get_common_calls = 0;
    mock_obj_type = 0;
    mock_get_common_status = status_$ok;
    mock_def_acldata_calls = 0;

    mock_prot_int_calls = 0;
    mock_prot_int_attr = 0;
    mock_prot_int_type = 0xFFFF;
    mock_prot_int_subsys = 0x55;
    mock_prot_int_acl0 = 0;
    mock_prot_int_acl1 = 0;

    mock_audit_calls = 0;

    mock_same_volume_result = 0;
    mock_same_volume_status = status_$ok;
    mock_same_volume_calls = 0;
    mock_loc_flags = 0;
    mock_get_location_status = status_$ok;

    mock_acl_check_result = 0;
    mock_acl_check_setid = 0;
    mock_acl_check_status = status_$ok;
    mock_acl_check_calls = 0;

    mock_locksmith_result = 0;
    mock_locksmith_calls = 0;
    mock_shutwired_calls = 0;
    mock_set_attribute_calls = 0;
    mock_set_attribute_status = status_$ok;
}

/* ==========================================================================
 * FILE_$SET_PROT: the default-protection selector (source-kzmp)
 * ========================================================================== */

/* Bit 24 of acl_uid->low asks for the object's default protection. */
TEST(default_selector_is_bit_24_of_low)
{
    uid_t file_uid = { 1, 2 };
    uint16_t prot_type = 0;
    uint8_t acl_data[44];
    uid_t acl_uid;
    status_$t status = 0;

    reset();
    memset(acl_data, 0x11, sizeof(acl_data));
    acl_uid.high = 0xAAAAAAAAu;
    acl_uid.low  = 0x01000000u;             /* bit 24 set */
    mock_obj_type = 0;                      /* -> type 6 */

    FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);

    ASSERT_EQ(1, mock_get_common_calls);    /* the nested proc ran */
    ASSERT_EQ(1, mock_def_acldata_calls);
    ASSERT_EQ(1, mock_prot_int_calls);
    ASSERT_EQ(6, mock_prot_int_type);
    ASSERT_EQ(0x03, mock_prot_int_attr);
}

/* Bit 8 is the bit the tree used to test; it must NOT trigger the path. */
TEST(bit_8_of_low_is_not_the_selector)
{
    uid_t file_uid = { 1, 2 };
    uint16_t prot_type = 2;
    uint32_t acl_data[11];
    uid_t acl_uid;
    status_$t status = 0;

    reset();
    memset(acl_data, 0x11, sizeof(acl_data));
    acl_uid.high = 0xAAAAAAAAu;
    acl_uid.low  = 0x00000100u;             /* bit 8 set, bit 24 clear */

    FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);

    ASSERT_EQ(0, mock_get_common_calls);    /* the nested proc must NOT run */
    ASSERT_EQ(1, mock_prot_int_calls);
    ASSERT_EQ(2, mock_prot_int_type);
    ASSERT_EQ(0x12, mock_prot_int_attr);    /* type 2 -> attr 0x12 */
}

/* The whole 0x0FF0 window is bits 20..31 of `low`; only bit 24 is tested. */
TEST(other_bits_in_the_selector_window_do_not_trigger)
{
    uid_t file_uid = { 1, 2 };
    uint16_t prot_type = 0;
    uint32_t acl_data[11];
    uid_t acl_uid;
    status_$t status = 0;
    uint32_t bit;

    for (bit = 20; bit < 32; bit++) {
        if (bit == 24) {
            continue;
        }
        reset();
        memset(acl_data, 0x11, sizeof(acl_data));
        acl_uid.high = 0;
        acl_uid.low  = 1u << bit;
        FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);
        ASSERT_EQ(0, mock_get_common_calls);
    }
}

/* 0x00E5DF6E `andi.b #-0x10,(-0x10,A6)` clears bits 24..27 of the copy. */
TEST(default_path_clears_bits_24_to_27_of_the_uid)
{
    uid_t file_uid = { 1, 2 };
    uint16_t prot_type = 0;
    uint32_t acl_data[11];
    uid_t acl_uid;
    status_$t status = 0;

    reset();
    memset(acl_data, 0x11, sizeof(acl_data));
    acl_uid.high = 0x12345678u;
    acl_uid.low  = 0xFFFFFFFFu;             /* bit 24 set, everything set */
    mock_obj_type = 0;

    FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);

    /* type 6 copies the local UID into acl[0]/acl[1] (0x00E5E016) */
    ASSERT_EQ(6, mock_prot_int_type);
    ASSERT_EQ(0x12345678u, mock_prot_int_acl0);
    ASSERT_EQ(0xF0FFFFFFu, mock_prot_int_acl1);
}

/* 0x00E5DF8E: a non-zero default protection is an invalid argument. */
TEST(non_zero_default_protection_is_invalid_arg)
{
    uid_t file_uid = { 1, 2 };
    uint16_t prot_type = 0;
    uint32_t acl_data[11];
    uid_t acl_uid;
    status_$t status = 0;

    reset();
    acl_uid.high = 0; acl_uid.low = 0x01000000u;
    mock_obj_type = 5;

    FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);

    ASSERT_EQ(0x000F0014, status);
    ASSERT_EQ(0, mock_prot_int_calls);
}

/* 0x00E5DFC2 `cmpi.w #0x7` / `bcc`: types 7 and up are rejected. */
TEST(type_seven_and_up_is_invalid_arg)
{
    uid_t file_uid = { 1, 2 };
    uint16_t prot_type = 7;
    uint32_t acl_data[11];
    uid_t acl_uid = { 0, 0 };
    status_$t status = 0;

    reset();
    FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);
    ASSERT_EQ(0x000F0014, status);
    ASSERT_EQ(0, mock_prot_int_calls);
}

/* 0x00E5DFE0-0x00E5E010: the jump table. */
TEST(type_to_attribute_mapping)
{
    static const uint16_t expect[7] = {
        0x10, 0x11, 0x12, 0x15, 0x13, 0x14, 0x03
    };
    uid_t file_uid = { 1, 2 };
    uid_t acl_uid = { 0, 0 };
    uint32_t acl_data[11];
    status_$t status;
    uint16_t t;

    for (t = 0; t < 7; t++) {
        uint16_t prot_type = t;
        reset();
        memset(acl_data, 0, sizeof(acl_data));
        FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);
        ASSERT_EQ(1, mock_prot_int_calls);
        ASSERT_EQ(expect[t], mock_prot_int_attr);
    }
}

/* 0x00E5E030 `clr.w -(SP)`: FILE_$SET_PROT never asks for the override. */
TEST(set_prot_passes_subsys_flag_false)
{
    uid_t file_uid = { 1, 2 };
    uint16_t prot_type = 1;
    uint32_t acl_data[11];
    uid_t acl_uid = { 0, 0 };
    status_$t status = 0;

    reset();
    memset(acl_data, 0, sizeof(acl_data));
    FILE_$SET_PROT(&file_uid, &prot_type, acl_data, &acl_uid, &status);
    ASSERT_EQ(0, mock_prot_int_subsys);
}

/* ==========================================================================
 * FILE_$SET_PROT_INT: the locksmith override (source-w7lk)
 * ========================================================================== */

#define SUBSYS_STATUS   status_$acl_no_right_to_set_subsystem_data

static status_$t run_prot_int(boolean subsys_flag, int16_t locksmith,
                              uint16_t proc_type)
{
    uid_t file_uid = { 1, 2 };
    uint8_t acl_data[64];
    status_$t status = 0;

    reset();
    memset(acl_data, 0, sizeof(acl_data));
    acl_data[0x2C] = 0;                 /* no ACL source UID */
    mock_loc_flags = 0;                 /* local file */
    mock_acl_check_result = 0;          /* >= 0: run the checks */
    mock_acl_check_setid = 0;           /* no "no rights" flag */
    mock_acl_check_status = SUBSYS_STATUS;
    mock_locksmith_result = locksmith;
    PROC1_$CURRENT = 3;
    PROC1_$TYPE[3] = proc_type;

    real_FILE_$SET_PROT_INT(&file_uid, acl_data, 0x10, 0, subsys_flag,
                            &status);
    return status;
}

/* 0x00E5DE82: a zero from ACL_$GET_LOCAL_LOCKSMITH forgives, whatever the
 * process type is. */
TEST(locksmith_zero_clears_the_status)
{
    ASSERT_EQ(status_$ok, run_prot_int(-1, 0, 9));
    ASSERT_EQ(status_$ok, run_prot_int(-1, 0, 1));
}

/*
 * THE regression: with a non-zero locksmith result the status is cleared when
 * the process type is NOT 9, and kept when it IS 9.
 */
TEST(non_locksmith_clears_only_when_type_is_not_nine)
{
    /* type 9 -> `beq 0x00E5DE9C` skips the clear, so the error stands */
    ASSERT_EQ(SUBSYS_STATUS, run_prot_int(-1, 1, 9));
    /* any other type -> 0x00E5DE9A clears it */
    ASSERT_EQ(status_$ok, run_prot_int(-1, 1, 1));
    ASSERT_EQ(status_$ok, run_prot_int(-1, 1, 0));
    ASSERT_EQ(status_$ok, run_prot_int(-1, 1, 8));
    ASSERT_EQ(status_$ok, run_prot_int(-1, 1, 10));
}

/* 0x00E5DE6A `tst.b D3b` / `bpl`: a false flag never reaches the override. */
TEST(false_subsys_flag_skips_the_override)
{
    ASSERT_EQ(SUBSYS_STATUS, run_prot_int(0, 0, 1));
    ASSERT_EQ(0, mock_locksmith_calls);
}

/* 0x00E5DE6E: only the 0x230010 status is forgiven. */
TEST(a_different_status_is_left_alone)
{
    uid_t file_uid = { 1, 2 };
    uint8_t acl_data[64];
    status_$t status = 0;

    reset();
    memset(acl_data, 0, sizeof(acl_data));
    mock_acl_check_status = 0x00230003;
    PROC1_$TYPE[3] = 1;

    real_FILE_$SET_PROT_INT(&file_uid, acl_data, 0x10, 0, -1, &status);

    ASSERT_EQ(0x00230003, status);
    ASSERT_EQ(0, mock_locksmith_calls);
    /* 0x00E5DE9C-0x00E5DEA6: a surviving error shuts wired pages down */
    ASSERT_EQ(1, mock_shutwired_calls);
    ASSERT_EQ(0, mock_set_attribute_calls);
}

/*
 * 0x00E5DE3A-0x00E5DE68, the OTHER locksmith test, whose sense is opposite:
 * a "no rights" flag plus process type 9 plus a NON-zero locksmith result
 * denies with 0x230001.
 */
TEST(no_rights_flag_denies_on_type_nine)
{
    uid_t file_uid = { 1, 2 };
    uint8_t acl_data[64];
    status_$t status = 0;

    reset();
    memset(acl_data, 0, sizeof(acl_data));
    mock_acl_check_setid = -1;          /* the "no rights" byte */
    mock_acl_check_status = status_$ok;
    mock_locksmith_result = 1;
    PROC1_$TYPE[3] = 9;

    real_FILE_$SET_PROT_INT(&file_uid, acl_data, 0x10, 0, 0, &status);
    ASSERT_EQ(0x00230001, status);

    /* type != 9 -> the deny is skipped entirely */
    reset();
    memset(acl_data, 0, sizeof(acl_data));
    mock_acl_check_setid = -1;
    mock_acl_check_status = status_$ok;
    mock_locksmith_result = 1;
    PROC1_$TYPE[3] = 1;
    status = 0;
    real_FILE_$SET_PROT_INT(&file_uid, acl_data, 0x10, 0, 0, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_set_attribute_calls);
}

int main(void)
{
    printf("FILE_$SET_PROT / FILE_$SET_PROT_INT tests\n");
    RUN_TEST(default_selector_is_bit_24_of_low);
    RUN_TEST(bit_8_of_low_is_not_the_selector);
    RUN_TEST(other_bits_in_the_selector_window_do_not_trigger);
    RUN_TEST(default_path_clears_bits_24_to_27_of_the_uid);
    RUN_TEST(non_zero_default_protection_is_invalid_arg);
    RUN_TEST(type_seven_and_up_is_invalid_arg);
    RUN_TEST(type_to_attribute_mapping);
    RUN_TEST(set_prot_passes_subsys_flag_false);
    RUN_TEST(locksmith_zero_clears_the_status);
    RUN_TEST(non_locksmith_clears_only_when_type_is_not_nine);
    RUN_TEST(false_subsys_flag_skips_the_override);
    RUN_TEST(a_different_status_is_left_alone);
    RUN_TEST(no_rights_flag_denies_on_type_nine);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
