/*
 * smd/test/test_scroll.c - unit tests for SMD_$START_SCROLL (0x00E15C68) and
 *                          SMD_$CONTINUE_SCROLL (0x00E15C9C)
 *
 * Both originals are hand-written assembly, transcribed in smd/sau2/scroll.s;
 * the C in smd/start_scroll.c and smd/continue_scroll.c is the portable model
 * compiled only when ARCH_M68K is not defined (bead source-a2ip), which is
 * exactly the configuration a host test builds.  Those two files are
 * #included below and the real entry points are called.
 *
 * Facts under test, each cited from the listing:
 *   - 0xE15C74 sets lock_state to 2 before anything else
 *   - 0xE15C7A ORs 0x20 into video_flags rather than replacing it
 *   - 0xE15C80 "clr.w (0x20,A1)" clears the WHOLE word, i.e. both bytes
 *   - 0xE15C84 copies the eventcount value to field_1c
 *   - 0xE15C8E/0xE15C92 fold video_flags and 0x8010 into the setup routine's
 *     result before it reaches ctrl_regs[0]
 *   - 0xE15CA8 "tst.w (0x24,A1)" - a zero remaining count finishes the scroll
 *     with lock_state 3 and writes NO register
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"

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
/* The routine the original reaches with "jsr (0x150,A5)"              */
/* ------------------------------------------------------------------ */

static int setup_calls;
static SMD_HW_REG_PTR setup_regs;
static smd_display_hw_t *setup_hw;
static uint16_t setup_result;

uint16_t smd_$setup_scroll_blt(SMD_HW_REG_PTR blt_regs, smd_display_hw_t *hw)
{
    setup_calls++;
    setup_regs = blt_regs;
    setup_hw = hw;
    return setup_result;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "../start_scroll.c"
#include "../continue_scroll.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static smd_display_hw_t hw;
static uint16_t regs[8];

static void setup(void)
{
    memset(&hw, 0, sizeof(hw));
    memset(regs, 0, sizeof(regs));
    setup_calls = 0;
    setup_regs = NULL;
    setup_hw = NULL;
    setup_result = 0x0004;
}

/* ------------------------------------------------------------------ */
/* SMD_$START_SCROLL                                                   */
/* ------------------------------------------------------------------ */

static void test_start_scroll_sets_state_and_starts_the_blt(void)
{
    setup();
    hw.lock_state = SMD_LOCK_STATE_LOCKED_5;
    hw.video_flags = 0x0041;
    hw.field_20 = (boolean)-1;
    hw.field_21 = 0x5A;
    hw.op_ec.value = 0x11223344;
    hw.field_1c = 0xDEADBEEF;

    SMD_$START_SCROLL(&hw, regs);

    CHECK_EQ(SMD_LOCK_STATE_SCROLL, hw.lock_state);   /* 0xE15C74 */
    CHECK_EQ(0x0061, hw.video_flags);                 /* 0xE15C7A ori.w #0x20 */
    CHECK_EQ(0, hw.field_20);                         /* 0xE15C80 clears both */
    CHECK_EQ(0, hw.field_21);                         /*          bytes       */
    CHECK_EQ(0x11223344, hw.field_1c);                /* 0xE15C84 */

    CHECK_EQ(1, setup_calls);
    CHECK_EQ((long)(uintptr_t)regs, (long)(uintptr_t)setup_regs);
    CHECK_EQ((long)(uintptr_t)&hw, (long)(uintptr_t)setup_hw);

    /* 0x0004 | video_flags(0x0061) | 0x8010 */
    CHECK_EQ(0x8075, regs[0]);
    /* no other register is written */
    CHECK_EQ(0, regs[1]);
}

/*
 * The OR at 0xE15C8E reads video_flags AFTER 0xE15C7A has set bit 5, so the
 * control word always carries it.
 */
static void test_start_scroll_control_word_carries_bit5(void)
{
    setup();
    hw.video_flags = 0;
    setup_result = 0x000C;

    SMD_$START_SCROLL(&hw, regs);

    CHECK_EQ(0x8000 | 0x0010 | 0x0020 | 0x000C, regs[0]);
}

/* ------------------------------------------------------------------ */
/* SMD_$CONTINUE_SCROLL                                                */
/* ------------------------------------------------------------------ */

/* 0xE15CA8: nothing left to scroll - lock_state 3 and no register write. */
static void test_continue_scroll_finished(void)
{
    setup();
    hw.field_24 = 0;
    hw.lock_state = SMD_LOCK_STATE_SCROLL;
    hw.video_flags = 0x0021;

    SMD_$CONTINUE_SCROLL(&hw, regs);

    CHECK_EQ(SMD_LOCK_STATE_SCROLL_DONE, hw.lock_state);
    CHECK_EQ(0, setup_calls);
    CHECK_EQ(0, regs[0]);
    /* video_flags is NOT touched on this path */
    CHECK_EQ(0x0021, hw.video_flags);
}

/*
 * 0xE15CB8: another step.  Note the ordering - the register write at
 * 0xE15CC4 happens BEFORE lock_state goes back to 2 at 0xE15CC6.
 */
static void test_continue_scroll_next_step(void)
{
    setup();
    hw.field_24 = 3;
    hw.lock_state = SMD_LOCK_STATE_SCROLL_DONE;
    hw.video_flags = 0x0020;
    setup_result = 0x0004;

    SMD_$CONTINUE_SCROLL(&hw, regs);

    CHECK_EQ(1, setup_calls);
    CHECK_EQ(0x8034, regs[0]);                        /* 0x04 | 0x20 | 0x8010 */
    CHECK_EQ(SMD_LOCK_STATE_SCROLL, hw.lock_state);   /* 0xE15CC6 */
    /* unlike START, CONTINUE does not touch video_flags, field_20 or
     * field_1c */
    CHECK_EQ(0x0020, hw.video_flags);
    CHECK_EQ(0, hw.field_1c);
}

/* field_24 is a WORD test, so a high bit alone still counts as "more". */
static void test_continue_scroll_word_test(void)
{
    setup();
    hw.field_24 = 0x8000;

    SMD_$CONTINUE_SCROLL(&hw, regs);

    CHECK_EQ(1, setup_calls);
    CHECK_EQ(SMD_LOCK_STATE_SCROLL, hw.lock_state);
}

int main(void)
{
    printf("SMD scroll tests\n");

    RUN_TEST(start_scroll_sets_state_and_starts_the_blt);
    RUN_TEST(start_scroll_control_word_carries_bit5);
    RUN_TEST(continue_scroll_finished);
    RUN_TEST(continue_scroll_next_step);
    RUN_TEST(continue_scroll_word_test);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
