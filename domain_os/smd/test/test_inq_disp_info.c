/*
 * smd/test/test_inq_disp_info.c - Unit tests for SMD_$INQ_DISP_INFO
 * (0x00E70124), with the real smd_$validate_unit (0x00E6D700) alongside it.
 *
 * Both real .c files are #included below and the real functions are called.
 *
 * Facts under test:
 *   - the result's +0x06 field is hw->max_x + 1 (the width) and +0x08 is
 *     hw->max_y + 1 (the height); the two names used to be swapped
 *     (0x00E7018A / 0x00E70194, bead source-5nq5)
 *   - +0x02/+0x04 receive the frame-buffer dimensions as one longword from
 *     the jump table at 0x00E701B2, not a bit depth and a plane count
 *   - display type 1 is 800x1024 (portrait) and type 2 is 1024x800
 *     (landscape), which is what the constant names now say (bead
 *     source-06qh); SMD_$INIT loads +0x50 with 0x31F for type 1
 *     (0x00E34E00) and 0x3FF for type 2 (0x00E34E0E)
 *   - the display info table is 1-based on the unit number
 *     (0x00E70172 "move.w (-0x60,A0,D1w*0x1),(A2)")
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

/* The result record has no pointers in it, so its host layout is its m68k
 * layout and these hold unconditionally. */
_Static_assert(offsetof(smd_disp_info_result_t, display_type) == 0x00, "+0");
_Static_assert(offsetof(smd_disp_info_result_t, mem_width) == 0x02, "+2");
_Static_assert(offsetof(smd_disp_info_result_t, mem_height) == 0x04, "+4");
_Static_assert(offsetof(smd_disp_info_result_t, width) == 0x06, "+6");
_Static_assert(offsetof(smd_disp_info_result_t, height) == 0x08, "+8");
_Static_assert(sizeof(smd_disp_info_result_t) == 10, "result size");

/* The two constants that bead source-06qh renamed keep their values. */
_Static_assert(SMD_DISP_TYPE_MONO_PORTRAIT == 1, "portrait is type 1");
_Static_assert(SMD_DISP_TYPE_MONO_LANDSCAPE == 2, "landscape is type 2");

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
uint8_t SMD_DISPLAY_UNITS[SMD_MAX_DISPLAY_UNITS * SMD_DISPLAY_UNIT_SIZE + 0x18];
smd_display_info_t SMD_DISPLAY_INFO[SMD_DISPLAY_INFO_COUNT];
uint16_t PROC1_$AS_ID;

static smd_display_hw_t test_hw;

/* ------------------------------------------------------------------ */
/* Functions under test                                                */
/* ------------------------------------------------------------------ */

#include "../validate_unit.c"
#include "../inq_disp_info.c"

/*
 * Configure unit 1 the way SMD_$INIT does for the given display type
 * (0x00E34E00 / 0x00E34E0E: type 1 gets max_x 0x31F and max_y 0x3FF, type 2
 * the other way round).
 */
static void setup(uint16_t display_type, int16_t max_x, int16_t max_y)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(SMD_DISPLAY_UNITS, 0, sizeof(SMD_DISPLAY_UNITS));
    memset(SMD_DISPLAY_INFO, 0, sizeof(SMD_DISPLAY_INFO));
    memset(&test_hw, 0, sizeof(test_hw));

    test_hw.display_type = display_type;
    test_hw.max_x = max_x;
    test_hw.max_y = max_y;
    smd_$unit_rec(1)->hw = &test_hw;
    /* 1-based: unit 1 is SMD_DISPLAY_INFO[0] */
    SMD_DISPLAY_INFO[0].display_type = display_type;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E7013E-0x00E70146: the whole result is cleared and the status zeroed
 * before anything else, and 0x00E70156 reports an invalid unit. */
static void test_invalid_unit_clears_the_result(void)
{
    uint16_t unit = 2;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT, 0x31F, 0x3FF);
    memset(&info, 0xAA, sizeof(info));

    SMD_$INQ_DISP_INFO(&unit, &info, &st);

    CHECK_EQ(status_$display_invalid_unit_number, st);
    CHECK_EQ(0, info.display_type);
    CHECK_EQ(0, info.mem_width);
    CHECK_EQ(0, info.mem_height);
    CHECK_EQ(0, info.width);
    CHECK_EQ(0, info.height);
}

/* An unconfigured unit 1 (display_type 0 in the info table) is invalid too:
 * smd_$validate_unit tests info[unit-1].display_type (0x00E6D722). */
static void test_unconfigured_unit_one_is_invalid(void)
{
    uint16_t unit = 1;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT, 0x31F, 0x3FF);
    SMD_DISPLAY_INFO[0].display_type = 0;

    SMD_$INQ_DISP_INFO(&unit, &info, &st);

    CHECK_EQ(status_$display_invalid_unit_number, st);
}

/*
 * Display type 1 is the 800x1024 portrait screen: SMD_$INIT gives it
 * max_x 0x31F and max_y 0x3FF, so the result must read 800 wide by 1024 high.
 * Under the old (swapped) field names this came out as 1024 wide by 800 high.
 */
static void test_portrait_dimensions(void)
{
    uint16_t unit = 1;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT, 0x31F, 0x3FF);

    SMD_$INQ_DISP_INFO(&unit, &info, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(SMD_DISP_TYPE_MONO_PORTRAIT, info.display_type);
    CHECK_EQ(800, info.width);
    CHECK_EQ(1024, info.height);
    /* 0x00E701C8 move.l #0x04000400 */
    CHECK_EQ(0x400, info.mem_width);
    CHECK_EQ(0x400, info.mem_height);
}

/* Display type 2 is the 1024x800 landscape screen. */
static void test_landscape_dimensions(void)
{
    uint16_t unit = 1;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_LANDSCAPE, 0x3FF, 0x31F);

    SMD_$INQ_DISP_INFO(&unit, &info, &st);

    CHECK_EQ(status_$ok, st);
    CHECK_EQ(1024, info.width);
    CHECK_EQ(800, info.height);
    CHECK_EQ(0x400, info.mem_width);
    CHECK_EQ(0x400, info.mem_height);
}

/* 0x00E701D2 move.l #0x04000800 for types 3 and 4. */
static void test_colour_frame_buffer_dimensions(void)
{
    uint16_t unit = 1;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(SMD_DISP_TYPE_COLOR_1024x2048, 0x3FF, 0x7FF);
    SMD_$INQ_DISP_INFO(&unit, &info, &st);
    CHECK_EQ(0x400, info.mem_width);
    CHECK_EQ(0x800, info.mem_height);

    setup(SMD_DISP_TYPE_COLOR_1024x2048_B, 0x3FF, 0x7FF);
    SMD_$INQ_DISP_INFO(&unit, &info, &st);
    CHECK_EQ(0x400, info.mem_width);
    CHECK_EQ(0x800, info.mem_height);
}

/* 0x00E701DC move.l #0x08000400 for types 5 and 9. */
static void test_hires_frame_buffer_dimensions(void)
{
    uint16_t unit = 1;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(SMD_DISP_TYPE_HI_RES_2048x1024, 0x7FF, 0x3FF);
    SMD_$INQ_DISP_INFO(&unit, &info, &st);
    CHECK_EQ(0x800, info.mem_width);
    CHECK_EQ(0x400, info.mem_height);

    setup(SMD_DISP_TYPE_HI_RES_2048x1024_B, 0x7FF, 0x3FF);
    SMD_$INQ_DISP_INFO(&unit, &info, &st);
    CHECK_EQ(0x800, info.mem_width);
    CHECK_EQ(0x400, info.mem_height);
}

/* The jump table at 0x00E701B2 sends type 7 to the function's exit, so its
 * frame-buffer dimensions stay zero while the visible ones are still filled
 * in.  Types above 11 fail the "cmpi.w #0xb" bound the same way. */
static void test_type_seven_and_out_of_range_leave_the_pair_zero(void)
{
    uint16_t unit = 1;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(7, 0x3FF, 0x3FF);
    SMD_$INQ_DISP_INFO(&unit, &info, &st);
    CHECK_EQ(status_$ok, st);
    CHECK_EQ(7, info.display_type);
    CHECK_EQ(1024, info.width);
    CHECK_EQ(1024, info.height);
    CHECK_EQ(0, info.mem_width);
    CHECK_EQ(0, info.mem_height);

    setup(12, 0x3FF, 0x3FF);
    SMD_$INQ_DISP_INFO(&unit, &info, &st);
    CHECK_EQ(0, info.mem_width);
    CHECK_EQ(0, info.mem_height);
}

/* The remaining "mono 1024x1024" types all share the 0x04000400 arm. */
static void test_remaining_mono_types(void)
{
    static const uint16_t types[] = {
        SMD_DISP_TYPE_MONO_1024x1024_A, SMD_DISP_TYPE_MONO_1024x1024_B,
        SMD_DISP_TYPE_MONO_1024x1024_C, SMD_DISP_TYPE_MONO_1024x1024_D
    };
    unsigned i;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        uint16_t unit = 1;
        smd_disp_info_result_t info;
        status_$t st = -1;

        setup(types[i], 0x3FF, 0x3FF);
        SMD_$INQ_DISP_INFO(&unit, &info, &st);

        CHECK_EQ(status_$ok, st);
        CHECK_EQ(0x400, info.mem_width);
        CHECK_EQ(0x400, info.mem_height);
    }
}

/* The display type in the result comes out of the info table, not out of the
 * hardware record: 0x00E70172 reads it before the record is even touched. */
static void test_display_type_comes_from_the_info_table(void)
{
    uint16_t unit = 1;
    smd_disp_info_result_t info;
    status_$t st = -1;

    setup(SMD_DISP_TYPE_MONO_PORTRAIT, 0x31F, 0x3FF);
    SMD_DISPLAY_INFO[0].display_type = SMD_DISP_TYPE_MONO_LANDSCAPE;

    SMD_$INQ_DISP_INFO(&unit, &info, &st);

    CHECK_EQ(SMD_DISP_TYPE_MONO_LANDSCAPE, info.display_type);
    /* ... while the dimensions still come from the hardware record */
    CHECK_EQ(800, info.width);
    CHECK_EQ(1024, info.height);
}

int main(void)
{
    printf("SMD_$INQ_DISP_INFO (0x00E70124) tests\n");

    RUN_TEST(invalid_unit_clears_the_result);
    RUN_TEST(unconfigured_unit_one_is_invalid);
    RUN_TEST(portrait_dimensions);
    RUN_TEST(landscape_dimensions);
    RUN_TEST(colour_frame_buffer_dimensions);
    RUN_TEST(hires_frame_buffer_dimensions);
    RUN_TEST(type_seven_and_out_of_range_leave_the_pair_zero);
    RUN_TEST(remaining_mono_types);
    RUN_TEST(display_type_comes_from_the_info_table);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
