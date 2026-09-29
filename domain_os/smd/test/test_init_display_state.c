/*
 * smd/test/test_init_display_state.c - Unit tests for
 * smd_$init_display_state (0x00E6F514).
 *
 * The real smd/init_display_state.c is #included below and the real
 * function is called; everything it depends on is mocked here.
 *
 * Facts under test:
 *   - the unit comes from SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID], reached
 *     with a word-scaled index (0x00E6F52A "add.w D0w,D0w" / 0x00E6F52C
 *     "move.w (0x48,A5,D0w*0x1),D3w")
 *   - unit == 0 returns status 0x00130004 ("invalid use of display driver
 *     procedure", SR10.4 status database) and acquires/releases nothing
 *     (0x00E6F532 then "bra.b 0x00e6f57a", past the SMD_$REL_DISPLAY at
 *     0x00E6F576)
 *   - the SMD_$ACQ_DISPLAY lock argument is the constant word 0 at
 *     0x00E6D92C = SMD_ACQ_LOCK_DATA (0x00E6F54C "pea (-0x1c22,PC)")
 *   - the acquire result is stored through the unit record's +0xFC
 *     controller-register pointer (0x00E6F556/0x00E6F55A)
 *   - `full` is forwarded verbatim to smd_$reset_unit_display
 *     (0x00E6F55C "move.b D2b,-(SP)")
 *   - SMD_$VIDEO_CTL runs only for a *true* Pascal boolean, i.e. < 0
 *     (0x00E6F566 "tst.b D2b" / 0x00E6F568 "bpl.b"), with the constant
 *     byte 0xFF at 0x00E6E458 = SMD_TRUE_DATA (0x00E6F56C "pea
 *     (-0x1116,PC)") and the caller's own status_ret
 *   - SMD_$REL_DISPLAY is reached on both `full` paths
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "smd/smd_internal.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-44s", #name);                                             \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed) {                                                 \
            tests_failed++;                                                   \
        } else {                                                              \
            printf("PASSED\n");                                               \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            if (!current_failed) printf("FAILED\n");                          \
            current_failed = 1;                                               \
            printf("      %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
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

/* The status the unit == 0 path writes, straight from the image
 * (0x00E6F532 "move.l #0x130004,(A2)"). */
_Static_assert(status_$display_invalid_use_of_driver_procedure == 0x00130004,
               "0x00E6F532 stores 0x00130004");

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
smd_$wired_data_t SMD_$WIRED_DATA;   /* unit 1's record */
uint16_t PROC1_$AS_ID;
uint16_t SMD_ACQ_LOCK_DATA = 0;
const boolean SMD_TRUE_DATA = (boolean)0xFF;

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int acq_calls, rel_calls, video_calls, reset_calls;
static int16_t *acq_lock_arg;
static uint16_t mock_acq_result;
static int16_t reset_unit_arg;
static boolean reset_full_arg;
static const uint8_t *video_flags_arg;
static status_$t *video_status_arg;

uint16_t SMD_$ACQ_DISPLAY(int16_t *lock_data)
{
    acq_lock_arg = lock_data;
    acq_calls++;
    return mock_acq_result;
}

void SMD_$REL_DISPLAY(void) { rel_calls++; }

void smd_$reset_unit_display(int16_t unit, boolean full)
{
    reset_unit_arg = unit;
    reset_full_arg = full;
    reset_calls++;
}

void SMD_$VIDEO_CTL(uint8_t *flags, status_$t *status_ret)
{
    video_flags_arg = flags;
    video_status_arg = status_ret;
    video_calls++;
}

/* The implementation under test. */
#include "smd/init_display_state.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

#define TEST_ASID 3
#define TEST_UNIT 1

static uint16_t test_ctrl_reg;

static void setup(uint16_t unit)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(&SMD_$WIRED_DATA, 0, sizeof(SMD_$WIRED_DATA));

    PROC1_$AS_ID = TEST_ASID;
    SMD_GLOBALS.asid_to_unit[TEST_ASID] = unit;

    test_ctrl_reg = 0;
    smd_$unit_rec(TEST_UNIT)->ctrl_regs = &test_ctrl_reg;

    acq_calls = rel_calls = video_calls = reset_calls = 0;
    acq_lock_arg = NULL;
    mock_acq_result = 0;
    reset_unit_arg = -1;
    reset_full_arg = 0;
    video_flags_arg = NULL;
    video_status_arg = NULL;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E6F532: no unit mapped for this ASID -> the driver-procedure error,
 * and nothing else happens at all. */
static void test_no_unit_returns_invalid_use(void)
{
    status_$t status = 0x5A5A5A5A;

    setup(0);
    smd_$init_display_state(0, &status);

    CHECK_EQ(status_$display_invalid_use_of_driver_procedure, status);
    CHECK_EQ(0, acq_calls);
    CHECK_EQ(0, reset_calls);
    CHECK_EQ(0, video_calls);
    /* The bra at 0x00E6F538 skips the SMD_$REL_DISPLAY at 0x00E6F576. */
    CHECK_EQ(0, rel_calls);
}

/* The index is word-scaled, so only this ASID's slot is consulted. */
static void test_unit_comes_from_this_asids_slot(void)
{
    status_$t status = 0x5A5A5A5A;

    setup(0);
    SMD_GLOBALS.asid_to_unit[TEST_ASID + 1] = TEST_UNIT;
    smd_$init_display_state(0, &status);

    CHECK_EQ(status_$display_invalid_use_of_driver_procedure, status);
    CHECK_EQ(0, acq_calls);
}

/* full == 0 (Pascal false): acquire, store, reset, release - no video. */
static void test_partial_init_skips_video_ctl(void)
{
    status_$t status = 0x5A5A5A5A;

    setup(TEST_UNIT);
    mock_acq_result = 0xBEEF;
    smd_$init_display_state(0, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(1, acq_calls);
    /* 0x00E6F54C: the lock word is the constant 0 at 0x00E6D92C. */
    CHECK((const void *)acq_lock_arg == (const void *)&SMD_ACQ_LOCK_DATA);
    CHECK_EQ(0, *acq_lock_arg);
    /* 0x00E6F55A: the acquire result lands in the record's +0xFC register. */
    CHECK_EQ(0xBEEF, test_ctrl_reg);
    CHECK_EQ(1, reset_calls);
    CHECK_EQ(TEST_UNIT, reset_unit_arg);
    CHECK_EQ(0, reset_full_arg);
    /* 0x00E6F568 "bpl" - a non-negative boolean is false. */
    CHECK_EQ(0, video_calls);
    CHECK_EQ(1, rel_calls);
}

/* full == 0xFF (Pascal true, i.e. < 0): the video-enable call is made,
 * with SMD_TRUE_DATA and the caller's status_ret, before the release. */
static void test_full_init_enables_video(void)
{
    status_$t status = 0x5A5A5A5A;

    setup(TEST_UNIT);
    mock_acq_result = 0x1234;
    smd_$init_display_state((int8_t)0xFF, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(1, acq_calls);
    CHECK_EQ(0x1234, test_ctrl_reg);
    CHECK_EQ(1, reset_calls);
    CHECK_EQ(TEST_UNIT, reset_unit_arg);
    /* 0x00E6F55C forwards the byte unchanged. */
    CHECK_EQ((int8_t)0xFF, (int8_t)reset_full_arg);
    CHECK_EQ(1, video_calls);
    /* 0x00E6F56C: the flags byte is the constant 0xFF at 0x00E6E458. */
    CHECK((const void *)video_flags_arg == (const void *)&SMD_TRUE_DATA);
    CHECK_EQ((uint8_t)0xFF, *video_flags_arg);
    /* 0x00E6F56A pushes A2, the caller's status_ret. */
    CHECK(video_status_arg == &status);
    CHECK_EQ(1, rel_calls);
}

/* A positive non-zero boolean is still false to "bpl" (0x00E6F568). */
static void test_positive_full_byte_is_false(void)
{
    status_$t status = 0x5A5A5A5A;

    setup(TEST_UNIT);
    smd_$init_display_state(0x01, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(1, reset_calls);
    CHECK_EQ(1, reset_full_arg);
    CHECK_EQ(0, video_calls);
    CHECK_EQ(1, rel_calls);
}

int main(void)
{
    printf("smd_$init_display_state (0x00E6F514) tests\n");

    RUN_TEST(no_unit_returns_invalid_use);
    RUN_TEST(unit_comes_from_this_asids_slot);
    RUN_TEST(partial_init_skips_video_ctl);
    RUN_TEST(full_init_enables_video);
    RUN_TEST(positive_full_byte_is_false);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
