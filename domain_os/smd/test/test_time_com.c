/*
 * smd/test/test_time_com.c - Unit tests for SMD_TIME_$COM (0x00E273D6) and
 * the code that drives it: SMD_$BLINK_CURSOR_CALLBACK (0x00E6FF56) and
 * SMD_$INIT_BLINK (0x00E34EB2).
 *
 * Both real .c files are #included below and the real functions are called.
 *
 * Facts under test (bead source-9j2l):
 *   - SMD_TIME_$COM is a 6-byte record at 0x00E273D6: it starts immediately
 *     after SMD_DISPLAY_INFO's single 0x60-byte entry (which ends at
 *     0x00E273D5) and ends before MNK_$KTT_PTRS at 0x00E273DC.  Only three
 *     displacements are ever used: +0x00 byte, +0x02 byte, +0x04 word.
 *   - +0x00 blink_enable gates the whole blink block
 *     (0x00E6FF72 "tst.b (A2)" / 0x00E6FF74 "bpl.b 0x00e6ffa0").
 *   - +0x04 blink_defer skips exactly one tick and is then cleared whatever
 *     its value was (0x00E6FF76 "tst.w (0x4,A2)" / 0x00E6FF7A "bne.b
 *     0x00e6ff9c" -> 0x00E6FF9C "clr.w (0x4,A2)").
 *   - +0x02 cursor_painted, tested AFTER the blink routine ran
 *     (0x00E6FF8E), selects the 250000us interval instead of 125000us
 *     (0x00E6FF64 / 0x00E6FF94).
 *   - SMD_$INIT_BLINK leaves blink_enable FALSE and cursor_painted TRUE
 *     (0x00E34EC6 "clr.b (A0)" / 0x00E34EC8 "st (0x2,A0)").
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

#define TEST(name) static void test_##name(void)
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

/*
 * The record is pointer-free, so its host layout is its m68k layout and these
 * hold unconditionally.
 */
_Static_assert(offsetof(smd_time_com_t, blink_enable) == 0x00, "+0");
_Static_assert(offsetof(smd_time_com_t, cursor_painted) == 0x02, "+2");
_Static_assert(offsetof(smd_time_com_t, blink_defer) == 0x04, "+4");
_Static_assert(sizeof(smd_time_com_t) == 6,
               "0x00E273D6..0x00E273DB (MNK_$KTT_PTRS is at 0x00E273DC)");

/*
 * And the neighbour that pins it down: SMD_DISPLAY_INFO must stop at
 * 0x00E273D5, i.e. it must hold exactly one 0x60-byte entry.
 */
_Static_assert(SMD_DISPLAY_INFO_COUNT == 1, "one display info entry");
/* Use the image stride, not sizeof: smd_display_hw_t holds pointers, so on a
 * 64-bit host it is wider than the 0x60 bytes the image uses. */
_Static_assert(0x00E27376 + SMD_DISPLAY_INFO_SIZE * SMD_DISPLAY_INFO_COUNT
                   == 0x00E273D6,
               "SMD_$DISPLAY_COM must end exactly where SMD_TIME_$COM starts");

/* The two intervals the callback picks between (0x1E848 / 0x3D090). */
#define IV_NORMAL   125000u
#define IV_SLOW     250000u

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
smd_$wired_data_t SMD_$WIRED_DATA;
smd_display_info_t SMD_DISPLAY_INFO[SMD_DISPLAY_INFO_COUNT];
smd_time_com_t SMD_TIME_$COM;
uint32_t TIME_$CLOCKH;
uint16_t PROC1_$AS_ID;

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int blink_calls;
static int reschedule_calls;
static uint32_t last_interval;
static int video_ctl_calls;
static uint8_t last_video_flag;
static int stop_tp_calls;
static uint16_t mock_disp_type;
static int inq_disp_type_calls;

/*
 * The unit-1 blink routine flips SMD_TIME_$COM.cursor_painted, exactly as
 * SMD_$BLINK_CURSOR_1 does with "not.b (A2)" at 0x00E27276.
 */
static void mock_blink_unit(void)
{
    blink_calls++;
    SMD_TIME_$COM.cursor_painted = (boolean)~SMD_TIME_$COM.cursor_painted;
}

void smd_$reschedule_blink_timer(uint32_t interval)
{
    reschedule_calls++;
    last_interval = interval;
}

void SMD_$VIDEO_CTL(uint8_t *flags, status_$t *status_ret)
{
    video_ctl_calls++;
    last_video_flag = *flags;
    *status_ret = 0;
}

void SMD_$STOP_TP_CURSOR(uint16_t *unit)
{
    (void)unit;
    stop_tp_calls++;
}

uint16_t SMD_$INQ_DISP_TYPE(uint16_t *unit)
{
    (void)unit;
    inq_disp_type_calls++;
    return mock_disp_type;
}

/* ------------------------------------------------------------------ */
/* Functions under test                                                */
/* ------------------------------------------------------------------ */

#include "../blink_cursor_callback.c"
#include "../init_blink.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static void setup(void)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(&SMD_TIME_$COM, 0, sizeof(SMD_TIME_$COM));
    memset(SMD_GLOBALS.blink_func, 0, sizeof(SMD_GLOBALS.blink_func));

    blink_calls = reschedule_calls = 0;
    last_interval = 0;
    video_ctl_calls = stop_tp_calls = inq_disp_type_calls = 0;
    last_video_flag = 0xAA;
    mock_disp_type = 0;
    TIME_$CLOCKH = 0;

    /*
     * The callback indexes the table with the unit number itself
     * ("lsl.l #0x2,D0" / "(0x1da0,A0)" at 0x00E6FF82-0x00E6FF88), and the
     * table holds 32-bit TARGET addresses.  Anchor ARCH_HOST_VA_BASE just
     * below the mock so the round trip through a 32-bit VA is exact on a
     * 64-bit host.
     */
    ARCH_HOST_VA_BASE = (uintptr_t)(void *)mock_blink_unit - 0x1000u;
    SMD_GLOBALS.default_unit = 1;
    SMD_GLOBALS.blink_func[1] = ARCH_PTR_TO_VA((void *)mock_blink_unit);

    /* Keep the blank-timeout and tp-cursor tails inert. */
    SMD_GLOBALS.blank_enabled = 0;       /* 0x00E6FFB2 tst.b / bpl -> skip */
    SMD_GLOBALS.blank_timeout = 0;
    SMD_GLOBALS.tp_cursor_timeout = -1;  /* 0x00E6FFFE tst.w / bmi -> skip */
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

TEST(blink_disabled_skips_everything)
{
    setup();
    SMD_TIME_$COM.blink_enable = 0;        /* tst.b (A2) / bpl */
    SMD_TIME_$COM.cursor_painted = true;
    SMD_TIME_$COM.blink_defer = 5;

    SMD_$BLINK_CURSOR_CALLBACK();

    CHECK_EQ(0, blink_calls);
    /* 0x00E6FFA0 is reached without passing 0x00E6FF9C, so the defer is
     * left alone when blinking is off. */
    CHECK_EQ(5, SMD_TIME_$COM.blink_defer);
    CHECK_EQ(1, reschedule_calls);
    CHECK_EQ(IV_NORMAL, last_interval);
}

TEST(defer_skips_one_tick_and_is_cleared)
{
    setup();
    SMD_TIME_$COM.blink_enable = true;
    SMD_TIME_$COM.cursor_painted = true;
    SMD_TIME_$COM.blink_defer = 7;         /* what SHOW_CURSOR writes */

    SMD_$BLINK_CURSOR_CALLBACK();

    /* 0x00E6FF7A branches straight to the clr.w, so no blink this tick... */
    CHECK_EQ(0, blink_calls);
    CHECK_EQ(0, SMD_TIME_$COM.blink_defer);
    /* ...and the interval stays the normal one: the cursor_painted test at
     * 0x00E6FF8E is inside the branch that was skipped. */
    CHECK_EQ(IV_NORMAL, last_interval);

    /* ...and the very next tick does blink. */
    SMD_$BLINK_CURSOR_CALLBACK();
    CHECK_EQ(1, blink_calls);
}

TEST(blink_runs_and_painted_selects_slow_interval)
{
    setup();
    SMD_TIME_$COM.blink_enable = true;
    SMD_TIME_$COM.cursor_painted = false;  /* the routine flips it to true */
    SMD_TIME_$COM.blink_defer = 0;

    SMD_$BLINK_CURSOR_CALLBACK();

    CHECK_EQ(1, blink_calls);
    CHECK_EQ((int8_t)0xFF, SMD_TIME_$COM.cursor_painted);
    CHECK_EQ(IV_SLOW, last_interval);
    CHECK_EQ(0, SMD_TIME_$COM.blink_defer);
}

TEST(blink_runs_and_erased_selects_normal_interval)
{
    setup();
    SMD_TIME_$COM.blink_enable = true;
    SMD_TIME_$COM.cursor_painted = true;   /* the routine flips it to false */
    SMD_TIME_$COM.blink_defer = 0;

    SMD_$BLINK_CURSOR_CALLBACK();

    CHECK_EQ(1, blink_calls);
    CHECK_EQ(0, SMD_TIME_$COM.cursor_painted);
    CHECK_EQ(IV_NORMAL, last_interval);
}

TEST(blink_enable_is_a_signed_domain_boolean)
{
    /* 0x00E6FF74 is "bpl", i.e. the test is on the SIGN of the byte; 0x7F
     * must NOT enable blinking even though it is non-zero. */
    setup();
    SMD_TIME_$COM.blink_enable = (boolean)0x7F;
    SMD_$BLINK_CURSOR_CALLBACK();
    CHECK_EQ(0, blink_calls);

    setup();
    SMD_TIME_$COM.blink_enable = (boolean)0x80;
    SMD_$BLINK_CURSOR_CALLBACK();
    CHECK_EQ(1, blink_calls);
}

TEST(tp_cursor_timeout_tail)
{
    setup();
    /* 0x00E6FFFE tst.w (0xe2,A5) / bmi: -1 is inert, 0 counts up. */
    SMD_GLOBALS.tp_cursor_timeout = 0;
    SMD_$BLINK_CURSOR_CALLBACK();
    CHECK_EQ(1, SMD_GLOBALS.tp_cursor_timeout);
    CHECK_EQ(0, stop_tp_calls);

    /* 0x00E70008 cmpi.w #0x2 / blt: the second tick reaches 2 and stops it. */
    SMD_$BLINK_CURSOR_CALLBACK();
    CHECK_EQ(2, SMD_GLOBALS.tp_cursor_timeout);
    CHECK_EQ(1, stop_tp_calls);
}

TEST(init_blink_leaves_blinking_off)
{
    setup();
    SMD_TIME_$COM.blink_enable = true;
    SMD_TIME_$COM.cursor_painted = false;
    SMD_TIME_$COM.blink_defer = 3;
    mock_disp_type = 0;                    /* no display -> no timer */

    SMD_$INIT_BLINK();

    CHECK_EQ(0, SMD_TIME_$COM.blink_enable);        /* clr.b (A0) */
    CHECK_EQ((int8_t)0xFF, SMD_TIME_$COM.cursor_painted); /* st (0x2,A0) */
    CHECK_EQ(0, SMD_TIME_$COM.blink_defer);         /* clr.w (0x4,A0) */
    CHECK_EQ(1, inq_disp_type_calls);
    CHECK_EQ(0, reschedule_calls);
}

TEST(init_blink_schedules_when_display_present)
{
    setup();
    mock_disp_type = SMD_DISP_TYPE_MONO_PORTRAIT;

    SMD_$INIT_BLINK();

    CHECK_EQ(1, reschedule_calls);
    CHECK_EQ(IV_NORMAL, last_interval);
}

int main(void)
{
    printf("test_time_com:\n");

    RUN_TEST(blink_disabled_skips_everything);
    RUN_TEST(defer_skips_one_tick_and_is_cleared);
    RUN_TEST(blink_runs_and_painted_selects_slow_interval);
    RUN_TEST(blink_runs_and_erased_selects_normal_interval);
    RUN_TEST(blink_enable_is_a_signed_domain_boolean);
    RUN_TEST(tp_cursor_timeout_tail);
    RUN_TEST(init_blink_leaves_blinking_off);
    RUN_TEST(init_blink_schedules_when_display_present);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
