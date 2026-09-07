/*
 * smd/test/test_blt.c - Unit tests for SMD_$BLT (0x00E6EC6E)
 *
 * The real smd/blt.c is #included below and the real SMD_$BLT is called;
 * everything it depends on is mocked in this translation unit.  The
 * interesting behaviour is the hardware BLT record it builds, which is
 * captured out of the mocked SMD_$START_BLT.
 *
 * Facts under test (all cited from the disassembly in blt.c):
 *   - bit 15 of the mode word lands in bit 15 of the control word, because
 *     the original sets it with a byte operation on the *high* byte of the
 *     word at A6-0x10 (0x00E6ECFC / 0x00E6ED06)
 *   - params[5] goes to record +0x04 and params[6] to +0x06 (0x00E6ED9C)
 *   - the display register base handed to SMD_$START_BLT is the unit
 *     record's +0xFC field (0x00E6EDD6)
 *   - the async path stores the ASID in the record's +0x08 field, not the
 *     owner ASID (0x00E6EDF8)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-44s", #name);                                             \
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
smd_display_info_t SMD_DISPLAY_INFO[SMD_DISPLAY_INFO_COUNT];
uint16_t PROC1_$AS_ID;
uint16_t SMD_ACQ_LOCK_DATA = 0;
int16_t SMD_SYNC_LOCK_DATA = 1;
int16_t SMD_ONE_LOCK_DATA = 1;

#define TEST_ASID 3
#define TEST_UNIT 1

static smd_display_hw_t test_hw;
static uint16_t test_ctrl_regs[8];

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int acq_calls, rel_calls, start_blt_calls;
static const int16_t *last_lock_data;
static uint16_t captured_words[8];  /* smd_hw_blt_t is local to blt.c */
static smd_display_hw_t *last_hw;
static SMD_HW_REG_PTR last_regs;

uint16_t SMD_$ACQ_DISPLAY(int16_t *lock_data)
{
    acq_calls++;
    last_lock_data = lock_data;
    return 0;
}

void SMD_$REL_DISPLAY(void) { rel_calls++; }

void SMD_$START_BLT(uint16_t *params, smd_display_hw_t *hw,
                    SMD_HW_REG_PTR hw_regs)
{
    start_blt_calls++;
    memcpy(captured_words, params, sizeof(captured_words));
    last_hw = hw;
    last_regs = hw_regs;
}

/* ------------------------------------------------------------------ */
/* The function under test                                             */
/* ------------------------------------------------------------------ */

#include "../blt.c"

#define captured (*(const smd_hw_blt_t *)captured_words)

static smd_display_unit_t *rec(void) { return smd_$unit_rec(TEST_UNIT); }

static void setup(void)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(SMD_DISPLAY_UNITS, 0, sizeof(SMD_DISPLAY_UNITS));
    memset(&test_hw, 0, sizeof(test_hw));
    memset(test_ctrl_regs, 0, sizeof(test_ctrl_regs));
    memset(captured_words, 0, sizeof(captured_words));

    PROC1_$AS_ID = TEST_ASID;
    SMD_GLOBALS.asid_to_unit[TEST_ASID] = TEST_UNIT;
    rec()->hw = &test_hw;
    rec()->ctrl_regs = test_ctrl_regs;

    acq_calls = rel_calls = start_blt_calls = 0;
    last_lock_data = NULL;
    last_hw = NULL;
    last_regs = NULL;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E6EC90: no unit for this ASID is "invalid use of driver procedure",
 * and nothing else happens - not even the display acquire. */
static void test_unit_zero_is_an_error(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    SMD_GLOBALS.asid_to_unit[TEST_ASID] = 0;

    SMD_$BLT(params, 0, 0, &st);

    CHECK_EQ(status_$display_invalid_use_of_driver_procedure, st);
    CHECK_EQ(0, acq_calls);
    CHECK_EQ(0, start_blt_calls);
}

/* 0x00E6ECCC-0x00E6ECE0: bits 7, 6 and 3 of the *low byte* of the mode word
 * are each rejected, and the display is acquired before the test and
 * released after it. */
static void test_invalid_mode_bits_are_rejected(void)
{
    static const uint16_t bad_modes[] = { 0x0080, 0x0040, 0x0008 };

    for (unsigned i = 0; i < sizeof(bad_modes) / sizeof(bad_modes[0]); i++) {
        uint16_t params[13] = {0};
        status_$t st = -1;

        setup();
        params[0] = bad_modes[i];

        SMD_$BLT(params, 0, 0, &st);

        CHECK_EQ(status_$display_invalid_blt_op, st);
        CHECK_EQ(1, acq_calls);
        CHECK_EQ(1, rel_calls);
        CHECK_EQ(0, start_blt_calls);
    }
}

/* Bit 15 of the mode word is *not* one of the rejected bits, even though the
 * low-byte test is a signed byte test: 0x8000 has a zero low byte. */
static void test_mode_bit15_is_not_a_low_byte_test(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    params[0] = 0x8000;

    SMD_$BLT(params, 0, 0, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(1, start_blt_calls);
    /* 0x00E6ECFC "andi.b #0x7f,(-0x10,A6)" then "or.b D1b,(-0x10,A6)" with
     * D1 = 0x80 acts on the high byte of the word, i.e. word bit 15. */
    CHECK_EQ(0x8000, captured.control);
}

/* 0x00E6ED10-0x00E6ED8E: the remaining control bits all live in the low
 * byte, and 0x00E6ED0A masks the word with 0x803F so nothing else survives. */
static void test_control_word_low_bits(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    params[0] = 0x0023;   /* bits 5, 1 and 0 */

    SMD_$BLT(params, 0, 0, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(0x0023, captured.control);
}

/* 0x00E6ED3C compares the *high* byte of params[1] against 2, and
 * 0x00E6ED52 the *low* byte of params[2] against 0x20. */
static void test_control_bits_from_the_parameter_bytes(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    params[1] = 0x0200;   /* high byte 0x02 -> control bit 3 */
    params[2] = 0x9920;   /* low byte 0x20 -> control bit 2 */

    SMD_$BLT(params, 0, 0, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(0x000C, captured.control);

    /* The other halves must not be looked at. */
    setup();
    params[1] = 0x0002;
    params[2] = 0x2000;
    SMD_$BLT(params, 0, 0, &st);
    CHECK_EQ(0x0000, captured.control);
}

/* 0x00E6ED9C "move.l (0xa,A2),(-0xc,A6)": params[5] lands at record +0x04,
 * params[6] at +0x06 - the order the old hand-written model had backwards. */
static void test_parameter_pair_order_and_extents(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    params[5] = 0x1234;
    params[6] = 0x5678;
    params[7] = 10;        /* y start */
    params[8] = 0x0050;    /* x start */
    params[11] = 20;
    params[12] = 0x00A3;   /* bit_pos 3, x end 0x0A0 */

    SMD_$BLT(params, 0, 0, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(0x1234, captured.field_04);
    CHECK_EQ(0x5678, captured.field_06);
    CHECK_EQ(3, captured.bit_pos);
    CHECK_EQ(10, captured.y_start);
    CHECK_EQ(0x0050, captured.x_start);
    /* -1 - |20 - 10| */
    CHECK_EQ((uint16_t)-11, captured.y_extent);
    /* -1 - |0x0A - 0x05| */
    CHECK_EQ((uint16_t)-6, captured.x_extent);
}

/* The extents are absolute values, so reversing the coordinates gives the
 * same answer (0x00E6EDAA / 0x00E6EDC6 "neg.w"). */
static void test_extents_are_absolute(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    params[7] = 20;
    params[8] = 0x00A0;
    params[11] = 10;
    params[12] = 0x0050;

    SMD_$BLT(params, 0, 0, &st);

    CHECK_EQ((uint16_t)-11, captured.y_extent);
    CHECK_EQ((uint16_t)-6, captured.x_extent);
}

/* 0x00E6ECB4-0x00E6ECC2: mode bit 4 chooses which constant word is passed to
 * SMD_$ACQ_DISPLAY. */
static void test_lock_word_selection(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    params[0] = 0x0000;
    SMD_$BLT(params, 0, 0, &st);
    CHECK_EQ((long)(intptr_t)&SMD_SYNC_LOCK_DATA, (long)(intptr_t)last_lock_data);

    setup();
    params[0] = 0x0010;
    SMD_$BLT(params, 0, 0, &st);
    CHECK_EQ((long)(intptr_t)&SMD_ACQ_LOCK_DATA, (long)(intptr_t)last_lock_data);
}

/* 0x00E6EDD6 pushes the record's +0xFC field by value and 0x00E6EDDA the
 * hardware record read from (-0xF4,A3). */
static void test_start_blt_gets_the_record_pointers(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    SMD_$BLT(params, 0, 0, &st);

    CHECK_EQ(1, start_blt_calls);
    CHECK_EQ((long)(intptr_t)&test_hw, (long)(intptr_t)last_hw);
    CHECK_EQ((long)(intptr_t)test_ctrl_regs, (long)(intptr_t)last_regs);
}

/* 0x00E6EDEA-0x00E6EDF8: the synchronous path releases the display, the
 * asynchronous one records the ASID in the record's +0x08 field instead. */
static void test_sync_releases_async_records_asid(void)
{
    uint16_t params[13] = {0};
    status_$t st = -1;

    setup();
    params[0] = 0x0000;
    SMD_$BLT(params, 0, 0, &st);
    CHECK_EQ(1, rel_calls);
    CHECK_EQ(0, rec()->field_08);
    CHECK_EQ(0, rec()->owner_asid);
    CHECK_EQ(status_$ok, st);

    setup();
    params[0] = 0x0010;
    SMD_$BLT(params, 0, 0, &st);
    CHECK_EQ(0, rel_calls);
    CHECK_EQ(TEST_ASID, rec()->field_08);
    /* the owner ASID is deliberately left alone */
    CHECK_EQ(0, rec()->owner_asid);
    CHECK_EQ(status_$ok, st);
}

int main(void)
{
    printf("SMD_$BLT (0x00E6EC6E) tests\n");

    RUN_TEST(unit_zero_is_an_error);
    RUN_TEST(invalid_mode_bits_are_rejected);
    RUN_TEST(mode_bit15_is_not_a_low_byte_test);
    RUN_TEST(control_word_low_bits);
    RUN_TEST(control_bits_from_the_parameter_bytes);
    RUN_TEST(parameter_pair_order_and_extents);
    RUN_TEST(extents_are_absolute);
    RUN_TEST(lock_word_selection);
    RUN_TEST(start_blt_gets_the_record_pointers);
    RUN_TEST(sync_releases_async_records_asid);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
