/*
 * smd/test/test_get_ec.c - Unit tests for SMD_$GET_EC (0x00E6FD90)
 *
 * The real smd/get_ec.c is #included below and the real SMD_$GET_EC is
 * called; only the globals and the one callee are mocked (bead source-ongi -
 * this file used to carry a private copy of the function body and a
 * mock_unit_aux_t / smd_get_unit_aux pair that no longer exist anywhere else,
 * so it exercised nothing).
 *
 * Facts under test, each tied to an instruction:
 *   0x00E6FDAA  the unit comes from SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID]
 *   0x00E6FDB0  unit 0 -> status 0x00130004 and no EC written
 *   0x00E6FDB8  otherwise the status is cleared first
 *   0x00E6FDD4  the hw record is fetched from the unit record's +0x00
 *               ("movea.l (-0xf4,A0),A3") before the key is examined
 *   0x00E6FDDA  keys are dispatched with an *unsigned* "cmpi.w #0x4" /
 *               "bcc", so 4 and above fall through to the error
 *   0x00E6FDF4  key 0 -> DTTE (A4 = 0x00E2DC90)
 *   0x00E6FDFA  key 1 -> hw + 0x10, i.e. hw->op_ec
 *   0x00E6FE02  key 2 -> 0x00E2E408, i.e. SMD_EC_2
 *   0x00E6FE0C  key 3 -> 0x00E1DC00, i.e. OS_$SHUTDOWN_EC
 *   0x00E6FE12  EC2_$REGISTER_EC1(ec1, status_ret)
 *   0x00E6FE1C  its result is stored through the second argument
 *   0x00E6FE20  an out-of-range key -> status 0x00130026, no EC written
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"
#include "os/os.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-52s", #name);                                             \
        current_failed = 0;                                                   \
        test_##name();                                                        \
        if (current_failed) {                                                 \
            tests_failed++;                                                   \
        } else {                                                              \
            tests_passed++;                                                   \
            printf("PASSED\n");                                               \
        }                                                                     \
    } while (0)

#define CHECK_EQ(expected, actual)                                            \
    do {                                                                      \
        long _e = (long)(expected);                                           \
        long _a = (long)(actual);                                             \
        if (_e != _a) {                                                       \
            if (!current_failed) printf("FAILED\n");                          \
            current_failed = 1;                                               \
            printf("      %s:%d: %s: expected 0x%lx, got 0x%lx\n", __FILE__,  \
                   __LINE__, #actual, (unsigned long)_e, (unsigned long)_a);  \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
uint8_t SMD_DISPLAY_UNITS[SMD_MAX_DISPLAY_UNITS * SMD_DISPLAY_UNIT_SIZE + 0x18];
smd_display_info_t SMD_DISPLAY_INFO[SMD_MAX_DISPLAY_UNITS];
uint16_t PROC1_$AS_ID;

ec_$eventcount_t DTTE;              /* 0x00E2DC90 */
ec_$eventcount_t OS_$SHUTDOWN_EC;   /* 0x00E1DC00 */

static smd_display_hw_t test_hw;

/* ------------------------------------------------------------------ */
/* Mocked callee                                                       */
/* ------------------------------------------------------------------ */

static ec_$eventcount_t *last_ec1;
static status_$t *last_status_arg;
static int register_calls;
static status_$t mock_register_status;

/* A distinctive non-null handle so "was it stored?" is unambiguous. */
static char mock_ec2_handle;

void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret)
{
    register_calls++;
    last_ec1 = ec1;
    last_status_arg = status_ret;
    *status_ret = mock_register_status;
    return &mock_ec2_handle;
}

/* ------------------------------------------------------------------ */
/* Function under test                                                 */
/* ------------------------------------------------------------------ */

#include "../get_ec.c"

/* ------------------------------------------------------------------ */

static void setup(uint16_t unit_for_asid)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(SMD_DISPLAY_UNITS, 0, sizeof(SMD_DISPLAY_UNITS));
    memset(SMD_DISPLAY_INFO, 0, sizeof(SMD_DISPLAY_INFO));
    memset(&test_hw, 0, sizeof(test_hw));
    memset(&DTTE, 0, sizeof(DTTE));
    memset(&OS_$SHUTDOWN_EC, 0, sizeof(OS_$SHUTDOWN_EC));

    PROC1_$AS_ID = 3;
    SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = unit_for_asid;
    smd_$unit_rec(1)->hw = &test_hw;

    last_ec1 = NULL;
    last_status_arg = NULL;
    register_calls = 0;
    mock_register_status = status_$ok;
}

/*
 * 0x00E6FDAE "bne.b" / 0x00E6FDB0 "move.l #0x130004,(A2)": with no display
 * bound to the calling ASID the routine reports an invalid use of the driver
 * procedure and returns without touching the EC output or calling
 * EC2_$REGISTER_EC1.
 */
static void test_no_display_for_asid(void)
{
    uint16_t key = 0;
    void *ec2 = (void *)(intptr_t)0x5A5A5A5A;
    status_$t status = 0x1234;

    setup(0);
    SMD_$GET_EC(&key, &ec2, &status);

    CHECK_EQ(status_$display_invalid_use_of_driver_procedure, status);
    CHECK_EQ(0, register_calls);
    CHECK_EQ((long)(intptr_t)0x5A5A5A5A, (long)(intptr_t)ec2);
}

/* 0x00E6FDF4 "pea (A4)" with A4 = 0x00E2DC90 */
static void test_key_0_is_dtte(void)
{
    uint16_t key = 0;
    void *ec2 = NULL;
    status_$t status = 0x1234;

    setup(1);
    SMD_$GET_EC(&key, &ec2, &status);

    CHECK_EQ(1, register_calls);
    CHECK_EQ((long)(intptr_t)&DTTE, (long)(intptr_t)last_ec1);
    CHECK_EQ((long)(intptr_t)&mock_ec2_handle, (long)(intptr_t)ec2);
    CHECK_EQ(status_$ok, status);
}

/* 0x00E6FDFA "pea (0x10,A3)" with A3 = unit_rec->hw */
static void test_key_1_is_hw_op_ec(void)
{
    uint16_t key = 1;
    void *ec2 = NULL;
    status_$t status = 0x1234;

    setup(1);
    SMD_$GET_EC(&key, &ec2, &status);

    CHECK_EQ(1, register_calls);
    CHECK_EQ((long)(intptr_t)&test_hw.op_ec, (long)(intptr_t)last_ec1);
    /* That op_ec really sits at hw+0x10 (the "pea (0x10,A3)") is enforced by
     * the _Static_assert in smd_internal.h under ARCH_M68K; it cannot be
     * checked here because ec_$eventcount_t holds host-width pointers. */
}

/* 0x00E6FE02 "pea (0xe2e408).l" - SMD_EC_2, which aliases the display-unit
 * block's second 12 bytes (bead source-ufwn). */
static void test_key_2_is_smd_ec_2(void)
{
    uint16_t key = 2;
    void *ec2 = NULL;
    status_$t status = 0x1234;

    setup(1);
    SMD_$GET_EC(&key, &ec2, &status);

    CHECK_EQ(1, register_calls);
    CHECK_EQ((long)(intptr_t)&SMD_EC_2, (long)(intptr_t)last_ec1);
    /* 0x00E2E408 - 0x00E2E3FC = 0x0C */
    CHECK_EQ(0x0C, (long)((char *)&SMD_EC_2 - (char *)SMD_DISPLAY_UNITS));
}

/* 0x00E6FE0C "move.l #0xe1dc00,-(SP)" */
static void test_key_3_is_shutdown_ec(void)
{
    uint16_t key = 3;
    void *ec2 = NULL;
    status_$t status = 0x1234;

    setup(1);
    SMD_$GET_EC(&key, &ec2, &status);

    CHECK_EQ(1, register_calls);
    CHECK_EQ((long)(intptr_t)&OS_$SHUTDOWN_EC, (long)(intptr_t)last_ec1);
}

/*
 * 0x00E6FDDA "cmpi.w #0x4,D0w" / "bcc.b 0x00e6fe20": an unsigned compare, so
 * every key from 4 up - including 0xFFFF - lands on the invalid-key error and
 * leaves the EC output alone.
 */
static void test_out_of_range_keys(void)
{
    static const uint16_t bad_keys[] = { 4, 5, 0x8000, 0xFFFF };
    unsigned i;

    for (i = 0; i < sizeof(bad_keys) / sizeof(bad_keys[0]); i++) {
        uint16_t key = bad_keys[i];
        void *ec2 = (void *)(intptr_t)0x5A5A5A5A;
        status_$t status = 0x1234;

        setup(1);
        SMD_$GET_EC(&key, &ec2, &status);

        CHECK_EQ(status_$display_invalid_event_count_key, status);
        CHECK_EQ(0, register_calls);
        CHECK_EQ((long)(intptr_t)0x5A5A5A5A, (long)(intptr_t)ec2);
    }
}

/*
 * 0x00E6FE12 "jsr EC2_$REGISTER_EC1" is passed the caller's own status_ret
 * (0x00E6FDF2 "pea (A2)"), so a failure there is what the caller sees - the
 * routine does not overwrite it afterwards.
 */
static void test_register_status_is_passed_through(void)
{
    uint16_t key = 2;
    void *ec2 = NULL;
    status_$t status = 0x1234;

    setup(1);
    mock_register_status = 0x00190005;
    SMD_$GET_EC(&key, &ec2, &status);

    CHECK_EQ(0x00190005, status);
    CHECK_EQ((long)(intptr_t)&status, (long)(intptr_t)last_status_arg);
}

/*
 * The hw pointer is loaded at 0x00E6FDD4, before the key is looked at, so a
 * key that does not use it still requires the unit record to be well formed.
 * This test just pins that the routine reads the record for the *bound* unit
 * rather than for a fixed unit 1.
 */
static void test_hw_comes_from_the_bound_unit(void)
{
    uint16_t key = 1;
    void *ec2 = NULL;
    status_$t status = 0x1234;
    static smd_display_hw_t other_hw;

    setup(2);
    memset(&other_hw, 0, sizeof(other_hw));
    smd_$unit_rec(2)->hw = &other_hw;

    SMD_$GET_EC(&key, &ec2, &status);

    CHECK_EQ((long)(intptr_t)&other_hw.op_ec, (long)(intptr_t)last_ec1);
}

int main(void)
{
    printf("SMD_$GET_EC (0x00E6FD90)\n");
    printf("========================\n");

    RUN_TEST(no_display_for_asid);
    RUN_TEST(key_0_is_dtte);
    RUN_TEST(key_1_is_hw_op_ec);
    RUN_TEST(key_2_is_smd_ec_2);
    RUN_TEST(key_3_is_shutdown_ec);
    RUN_TEST(out_of_range_keys);
    RUN_TEST(register_status_is_passed_through);
    RUN_TEST(hw_comes_from_the_bound_unit);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
