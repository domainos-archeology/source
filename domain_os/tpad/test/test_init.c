/*
 * tpad/test/test_init.c - Unit tests for TPAD_$INIT (0x00E33570)
 *
 * The real tpad/init.c is #included below and the real TPAD_$INIT is called;
 * only SMD_$N_DEVICES, SMD_$INQ_DISP_INFO, TPAD_$SET_UNIT and TIME_$CLOCK are
 * mocked.
 *
 * The point of these tests is bead source-j999: which of the per-unit config
 * fields TPAD_$INIT writes, and which way round the conversion factors are
 * computed.  The evidence, with A3 = config + 0x2C throughout the loop so a
 * displacement of -0x2C + k names config offset k:
 *
 *   0x00E335C6  move.w (-0x8,A6),D0w / subq.w #1 / move.w D0w,(-0x14,A3)
 *               config +0x18 (y_max_disp) = disp_info.height - 1
 *   0x00E335D0  move.w (-0xa,A6),D0w / subq.w #1 / move.w D0w,(-0x18,A3)
 *               config +0x14 (x_max_disp) = disp_info.width - 1
 *   0x00E335DA  move.w (-0x18,A3),(-0x26,A3)   config +0x06 = config +0x14
 *   0x00E335E0  move.w (-0x14,A3),(-0x24,A3)   config +0x08 = config +0x18
 *   0x00E335E6  tst.w (-0x26,A3) / 0x00E335EC move.w #0x400,(-0x10,A3)
 *   0x00E335F4  move.w (-0x22,A3),D0w / ext.l / divs.w (-0x26,A3),D0
 *   0x00E335FE  move.w D0w,(-0x10,A3)          x_factor = x_range / x_scale
 *   0x00E33602-0x00E3361A                      the same for Y
 *
 * +0x06/+0x08 are the *scale* pair and +0x0A/+0x0C the *range* pair, which is
 * what TPAD_$SET_UNIT_MODE (0x00E69838 / 0x00E69840 write its xs/ys arguments
 * to +0x06/+0x08) and TPAD_$RE_RANGE_UNIT (0x00E69A74 / 0x00E69A7A seed
 * +0x0A/+0x0C with 0x200) independently confirm.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "tpad/tpad_internal.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-46s", #name);                                             \
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
            printf("      %s:%d: %s: expected %ld, got %ld\n", __FILE__,      \
                   __LINE__, #actual, _e, _a);                                \
        }                                                                     \
    } while (0)

/* The per-unit config record has no pointers, so its host layout is its m68k
 * layout and the displacements quoted above hold here too. */
_Static_assert(offsetof(tpad_$unit_config_t, mode) == 0x04, "+0x04 mode");
_Static_assert(offsetof(tpad_$unit_config_t, x_scale) == 0x06, "+0x06 x_scale");
_Static_assert(offsetof(tpad_$unit_config_t, y_scale) == 0x08, "+0x08 y_scale");
_Static_assert(offsetof(tpad_$unit_config_t, x_range) == 0x0A, "+0x0A x_range");
_Static_assert(offsetof(tpad_$unit_config_t, y_range) == 0x0C, "+0x0C y_range");
_Static_assert(offsetof(tpad_$unit_config_t, x_max_disp) == 0x14, "+0x14");
_Static_assert(offsetof(tpad_$unit_config_t, y_max_disp) == 0x18, "+0x18");
_Static_assert(offsetof(tpad_$unit_config_t, x_factor) == 0x1C, "+0x1C");
_Static_assert(offsetof(tpad_$unit_config_t, y_factor) == 0x1E, "+0x1E");
_Static_assert(sizeof(tpad_$unit_config_t) == 0x2C, "config is 0x2C bytes");

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

tpad_$unit_config_t tpad_$unit_configs[TPAD_$MAX_UNITS];
tpad_$globals_t tpad_$globals;

/* 0x00E33578 "pea (0xf0,PC)" -> 0x00E3366A, a code-region word holding
 * 0x0001 (read with gsk). */
int16_t tpad_$unit_num_for_init = 1;

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int16_t mock_n_devices;
static uint16_t mock_disp_type;
static uint16_t mock_width;
static uint16_t mock_height;

static int16_t *last_set_unit_arg;
static int set_unit_calls;
static int clock_calls;
static clock_t *last_clock_arg;
static uint16_t last_inq_unit;
static int inq_calls;

void TPAD_$SET_UNIT(int16_t *unitnum)
{
    set_unit_calls++;
    last_set_unit_arg = unitnum;
}

uint16_t SMD_$N_DEVICES(void)
{
    return (uint16_t)mock_n_devices;
}

void SMD_$INQ_DISP_INFO(uint16_t *unit, smd_disp_info_result_t *info,
                        status_$t *status_ret)
{
    inq_calls++;
    last_inq_unit = *unit;
    memset(info, 0, sizeof(*info));
    info->display_type = mock_disp_type;
    info->width = mock_width;
    info->height = mock_height;
    *status_ret = status_$ok;
}

void TIME_$CLOCK(clock_t *clock)
{
    clock_calls++;
    last_clock_arg = clock;
}

/* ------------------------------------------------------------------ */
/* Function under test                                                 */
/* ------------------------------------------------------------------ */

#include "../init.c"

/* ------------------------------------------------------------------ */

static void setup(int16_t n_devices, uint16_t disp_type,
                  uint16_t width, uint16_t height)
{
    memset(tpad_$unit_configs, 0, sizeof(tpad_$unit_configs));
    memset(&tpad_$globals, 0, sizeof(tpad_$globals));

    mock_n_devices = n_devices;
    mock_disp_type = disp_type;
    mock_width = width;
    mock_height = height;

    set_unit_calls = 0;
    last_set_unit_arg = NULL;
    inq_calls = 0;
    last_inq_unit = 0;
    clock_calls = 0;
    last_clock_arg = NULL;
}

/*
 * The 800x1024 portrait display (SMD_$INIT 0x00E34E00 gives type 1 max_x
 * 0x31F, max_y 0x3FF, so SMD_$INQ_DISP_INFO reports width 800, height 1024).
 * With the ranges still zero the factors take the 0x400 default only if the
 * *scale* is zero, and here the scale has just been set from the bounds, so
 * both divisions run: 0 / 799 and 0 / 1023.
 */
static void test_bounds_go_to_max_disp_and_scale(void)
{
    setup(1, 1, 800, 1024);

    TPAD_$INIT();

    CHECK_EQ(1, inq_calls);
    CHECK_EQ(1, last_inq_unit);

    /* 0x00E335D0 / 0x00E335C6 */
    CHECK_EQ(799, tpad_$unit_configs[0].x_max_disp);
    CHECK_EQ(1023, tpad_$unit_configs[0].y_max_disp);

    /* 0x00E335DA / 0x00E335E0: the copies land in the SCALE fields */
    CHECK_EQ(799, tpad_$unit_configs[0].x_scale);
    CHECK_EQ(1023, tpad_$unit_configs[0].y_scale);

    /* and the range fields are untouched */
    CHECK_EQ(0, tpad_$unit_configs[0].x_range);
    CHECK_EQ(0, tpad_$unit_configs[0].y_range);

    /* 0x00E335F4 / 0x00E33610: factor = range / scale */
    CHECK_EQ(0, tpad_$unit_configs[0].x_factor);
    CHECK_EQ(0, tpad_$unit_configs[0].y_factor);
}

/*
 * With a non-zero range seeded first, the divisions are visibly
 * range / scale and not scale / range: 0x200 / 799 truncates to 0, while
 * 799 / 0x200 would be 1.
 */
static void test_factor_is_range_over_scale(void)
{
    setup(1, 1, 512, 256);

    tpad_$unit_configs[0].x_range = 1024;
    tpad_$unit_configs[0].y_range = 1024;

    TPAD_$INIT();

    CHECK_EQ(511, tpad_$unit_configs[0].x_scale);
    CHECK_EQ(255, tpad_$unit_configs[0].y_scale);
    CHECK_EQ(1024 / 511, tpad_$unit_configs[0].x_factor);  /* 2, not 0 */
    CHECK_EQ(1024 / 255, tpad_$unit_configs[0].y_factor);  /* 4, not 0 */
}

/*
 * 0x00E335C0 "tst.w (-0x10,A6)" / "beq": when the display type is zero the
 * bounds are left alone, so the scale copies pick up whatever was already in
 * x_max_disp / y_max_disp.
 */
static void test_display_type_zero_keeps_the_bounds(void)
{
    setup(1, 0, 800, 1024);

    tpad_$unit_configs[0].x_max_disp = 100;
    tpad_$unit_configs[0].y_max_disp = 200;

    TPAD_$INIT();

    CHECK_EQ(100, tpad_$unit_configs[0].x_max_disp);
    CHECK_EQ(200, tpad_$unit_configs[0].y_max_disp);
    CHECK_EQ(100, tpad_$unit_configs[0].x_scale);
    CHECK_EQ(200, tpad_$unit_configs[0].y_scale);
}

/*
 * 0x00E335E6 / 0x00E33602: a zero scale short-circuits to the 0x400 default
 * instead of dividing.
 */
static void test_zero_scale_gives_the_default_factor(void)
{
    setup(1, 1, 1, 1);   /* width - 1 == 0 and height - 1 == 0 */

    tpad_$unit_configs[0].x_range = 1234;
    tpad_$unit_configs[0].y_range = 5678;

    TPAD_$INIT();

    CHECK_EQ(0, tpad_$unit_configs[0].x_scale);
    CHECK_EQ(0, tpad_$unit_configs[0].y_scale);
    CHECK_EQ(TPAD_$FACTOR_DEFAULT, tpad_$unit_configs[0].x_factor);
    CHECK_EQ(TPAD_$FACTOR_DEFAULT, tpad_$unit_configs[0].y_factor);
}

/*
 * 0x00E3358A "subq.w #0x1,D0w" / "slt D3b" / "bmi" skips the loop entirely
 * when SMD_$N_DEVICES returns 0, and the dbf at 0x00E33624 otherwise runs it
 * exactly n times for units 1..n.
 */
static void test_zero_devices_runs_no_iterations(void)
{
    setup(0, 1, 800, 1024);

    TPAD_$INIT();

    CHECK_EQ(0, inq_calls);
    CHECK_EQ(0, tpad_$unit_configs[0].x_scale);
    /* the globals are still initialised */
    CHECK_EQ(TPAD_$DEFAULT_CURSOR_Y, tpad_$globals.cursor_y);
}

static void test_two_devices_configure_both(void)
{
    setup(2, 1, 800, 1024);

    TPAD_$INIT();

    CHECK_EQ(2, inq_calls);
    CHECK_EQ(2, last_inq_unit);
    CHECK_EQ(799, tpad_$unit_configs[0].x_scale);
    CHECK_EQ(799, tpad_$unit_configs[1].x_scale);
    CHECK_EQ(0, tpad_$unit_configs[2].x_scale);
}

/*
 * The epilogue, 0x00E33632-0x00E3365C.  Note the assembly order: TIME_$CLOCK
 * first, then cursor_y, cursor_x, button_state, accum_x, accum_y,
 * touchpad_max, dev_type.
 */
static void test_global_initialisation(void)
{
    setup(1, 1, 800, 1024);

    tpad_$globals.button_state = 0x1111;
    tpad_$globals.accum_x = 0x2222;
    tpad_$globals.accum_y = 0x3333;
    tpad_$globals.dev_type = tpad_$have_mouse;

    TPAD_$INIT();

    CHECK_EQ(1, clock_calls);
    CHECK_EQ((long)(intptr_t)&tpad_$globals.last_clock,
             (long)(intptr_t)last_clock_arg);
    CHECK_EQ(TPAD_$DEFAULT_CURSOR_Y, tpad_$globals.cursor_y);      /* 0x160 */
    CHECK_EQ(TPAD_$DEFAULT_CURSOR_X, tpad_$globals.cursor_x);      /* 0x162 */
    CHECK_EQ(0, tpad_$globals.button_state);                       /* 0x172 */
    CHECK_EQ(0, tpad_$globals.accum_x);                            /* 0x17A */
    CHECK_EQ(0, tpad_$globals.accum_y);                            /* 0x178 */
    CHECK_EQ(TPAD_$DEFAULT_TOUCHPAD_MAX, tpad_$globals.touchpad_max); /* 0x16A */
    CHECK_EQ(tpad_$unknown, tpad_$globals.dev_type);               /* 0x16C */
}

/* 0x00E33578 "pea (0xf0,PC)": the address of the constant word 1. */
static void test_set_unit_gets_the_init_unit_cell(void)
{
    setup(1, 1, 800, 1024);

    TPAD_$INIT();

    CHECK_EQ(1, set_unit_calls);
    CHECK_EQ((long)(intptr_t)&tpad_$unit_num_for_init,
             (long)(intptr_t)last_set_unit_arg);
    CHECK_EQ(1, tpad_$unit_num_for_init);
}

int main(void)
{
    printf("TPAD_$INIT (0x00E33570)\n");
    printf("=======================\n");

    RUN_TEST(bounds_go_to_max_disp_and_scale);
    RUN_TEST(factor_is_range_over_scale);
    RUN_TEST(display_type_zero_keeps_the_bounds);
    RUN_TEST(zero_scale_gives_the_default_factor);
    RUN_TEST(zero_devices_runs_no_iterations);
    RUN_TEST(two_devices_configure_both);
    RUN_TEST(global_initialisation);
    RUN_TEST(set_unit_gets_the_init_unit_cell);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
