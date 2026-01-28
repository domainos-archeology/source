/*
 * smd/test/test_get_ec.c - Unit tests for SMD_$GET_EC
 *
 * Tests event count retrieval for all key values (0-3) plus invalid keys,
 * and verifies the unit==0 error path.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Minimal type stubs for native compilation */
typedef long status_$t;
#define status_$ok 0
#define status_$display_invalid_use_of_driver_procedure 0x00130004
#define status_$display_invalid_event_count_key 0x00130026

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

#define ASSERT_NEQ(not_expected, actual) do { \
    if ((not_expected) == (actual)) { \
        printf("FAILED\n    Expected not 0x%lx at line %d\n", \
               (unsigned long)(not_expected), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * Mock data structures
 */

/* Event count - minimal mock (12 bytes like ec_$eventcount_t) */
typedef struct {
    uint32_t value;
    uint32_t head;
    uint32_t tail;
} mock_ec_t;

/* Display hardware info - minimal mock matching smd_display_hw_t */
typedef struct {
    uint16_t display_type;
    uint16_t lock_state;
    mock_ec_t lock_ec;    /* 0x04 */
    mock_ec_t op_ec;      /* 0x10 */
    /* rest not needed for this test */
} mock_hw_t;

/* Unit auxiliary data */
typedef struct {
    mock_hw_t *hw;
    uint16_t owner_asid;
    uint16_t borrowed_asid;
} mock_unit_aux_t;

/* SMD globals - minimal */
#define MOCK_MAX_ASIDS 256
typedef struct {
    uint8_t pad_00[0x48];
    uint16_t asid_to_unit[MOCK_MAX_ASIDS];
} mock_smd_globals_t;

/* Mock globals */
static mock_smd_globals_t mock_globals;
static mock_hw_t mock_hw;
static mock_unit_aux_t mock_unit_aux;
static mock_ec_t mock_dtte;
static mock_ec_t mock_smd_ec_2;
static mock_ec_t mock_shutdown_ec;
static uint16_t mock_as_id;

/* Last EC1 passed to EC2_$REGISTER_EC1 */
static void *last_register_ec1 = NULL;
static void *mock_ec2_handle = (void *)0xBEEF;

/*
 * Redefine external references to use mocks
 */
#define SMD_GLOBALS mock_globals
#define PROC1_$AS_ID mock_as_id
#define DTTE mock_dtte
#define SMD_EC_2 mock_smd_ec_2
#define OS_$SHUTDOWN_EC mock_shutdown_ec
#define SMD_DISPLAY_UNIT_SIZE 0x10C
#define SMD_UNIT_AUX_BASE 0x00E2E308

/* Mock smd_get_unit_aux */
static mock_unit_aux_t *smd_get_unit_aux(uint16_t unit_num) {
    (void)unit_num;
    return &mock_unit_aux;
}

/* Mock EC2_$REGISTER_EC1 */
static void *EC2_$REGISTER_EC1(void *ec1, status_$t *status_ret) {
    last_register_ec1 = ec1;
    *status_ret = status_$ok;
    return mock_ec2_handle;
}

/* Typedefs to satisfy function under test */
typedef mock_ec_t ec_$eventcount_t;
typedef mock_hw_t smd_display_hw_t;
typedef mock_unit_aux_t smd_unit_aux_t;

/*
 * Event count key values (from get_ec.c)
 */
#define SMD_EC_KEY_DTTE     0
#define SMD_EC_KEY_DISP_OP  1
#define SMD_EC_KEY_SMD_EC2  2
#define SMD_EC_KEY_SHUTDOWN 3

/*
 * Function under test - reimplemented with mocks
 */
void SMD_$GET_EC(uint16_t *key, void **ec2_ret, status_$t *status_ret)
{
    uint16_t unit;
    smd_unit_aux_t *aux;
    ec_$eventcount_t *ec1;
    smd_display_hw_t *hw;

    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    *status_ret = status_$ok;

    aux = smd_get_unit_aux(unit);
    hw = aux->hw;

    switch (*key) {
    case SMD_EC_KEY_DTTE:
        ec1 = &DTTE;
        break;
    case SMD_EC_KEY_DISP_OP:
        ec1 = &hw->op_ec;
        break;
    case SMD_EC_KEY_SMD_EC2:
        ec1 = &SMD_EC_2;
        break;
    case SMD_EC_KEY_SHUTDOWN:
        ec1 = &OS_$SHUTDOWN_EC;
        break;
    default:
        *status_ret = status_$display_invalid_event_count_key;
        return;
    }

    *ec2_ret = EC2_$REGISTER_EC1(ec1, status_ret);
}

/*
 * Test setup helper
 */
static void setup(void)
{
    memset(&mock_globals, 0, sizeof(mock_globals));
    memset(&mock_hw, 0, sizeof(mock_hw));
    memset(&mock_unit_aux, 0, sizeof(mock_unit_aux));
    memset(&mock_dtte, 0, sizeof(mock_dtte));
    memset(&mock_smd_ec_2, 0, sizeof(mock_smd_ec_2));
    memset(&mock_shutdown_ec, 0, sizeof(mock_shutdown_ec));

    mock_unit_aux.hw = &mock_hw;
    mock_as_id = 1;
    mock_globals.asid_to_unit[1] = 1; /* ASID 1 -> unit 1 */
    last_register_ec1 = NULL;
}

/*
 * Tests
 */

TEST(key0_dtte)
{
    setup();
    uint16_t key = SMD_EC_KEY_DTTE;
    void *ec2 = NULL;
    status_$t st = -1;

    SMD_$GET_EC(&key, &ec2, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&mock_dtte, (unsigned long)last_register_ec1);
    ASSERT_EQ((unsigned long)mock_ec2_handle, (unsigned long)ec2);
}

TEST(key1_disp_op)
{
    setup();
    uint16_t key = SMD_EC_KEY_DISP_OP;
    void *ec2 = NULL;
    status_$t st = -1;

    SMD_$GET_EC(&key, &ec2, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&mock_hw.op_ec, (unsigned long)last_register_ec1);
    ASSERT_EQ((unsigned long)mock_ec2_handle, (unsigned long)ec2);
}

TEST(key2_smd_ec2)
{
    setup();
    uint16_t key = SMD_EC_KEY_SMD_EC2;
    void *ec2 = NULL;
    status_$t st = -1;

    SMD_$GET_EC(&key, &ec2, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&mock_smd_ec_2, (unsigned long)last_register_ec1);
}

TEST(key3_shutdown)
{
    setup();
    uint16_t key = SMD_EC_KEY_SHUTDOWN;
    void *ec2 = NULL;
    status_$t st = -1;

    SMD_$GET_EC(&key, &ec2, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&mock_shutdown_ec, (unsigned long)last_register_ec1);
}

TEST(key_invalid)
{
    setup();
    uint16_t key = 4;
    void *ec2 = NULL;
    status_$t st = -1;

    SMD_$GET_EC(&key, &ec2, &st);

    ASSERT_EQ(status_$display_invalid_event_count_key, st);
    /* EC2 should not have been set */
    ASSERT_EQ((unsigned long)NULL, (unsigned long)ec2);
}

TEST(key_large_invalid)
{
    setup();
    uint16_t key = 0xFF;
    void *ec2 = NULL;
    status_$t st = -1;

    SMD_$GET_EC(&key, &ec2, &st);

    ASSERT_EQ(status_$display_invalid_event_count_key, st);
}

TEST(unit_zero_error)
{
    setup();
    mock_globals.asid_to_unit[1] = 0; /* No display for this ASID */
    uint16_t key = SMD_EC_KEY_DTTE;
    void *ec2 = NULL;
    status_$t st = -1;

    SMD_$GET_EC(&key, &ec2, &st);

    ASSERT_EQ(status_$display_invalid_use_of_driver_procedure, st);
}

int main(void)
{
    printf("test_get_ec:\n");

    RUN_TEST(key0_dtte);
    RUN_TEST(key1_disp_op);
    RUN_TEST(key2_smd_ec2);
    RUN_TEST(key3_shutdown);
    RUN_TEST(key_invalid);
    RUN_TEST(key_large_invalid);
    RUN_TEST(unit_zero_error);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
