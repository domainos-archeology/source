/*
 * dtty/test/test_init.c - Unit tests for DTTY_$INIT (0x00E34BD0).
 *
 * The real dtty/init.c is #included below; SMD_$INQ_DISP_TYPE, SMD_$ASSOC,
 * dtty_$clear_window, dtty_$load_font and dtty_$report_error are mocked.
 *
 * Facts under test (bead source-4km0):
 *   - `st (0x6,A2)` and `clr.b (0x4,A2)` at 0x00E34BF2/0x00E34BF6 really do
 *     write the two module-block flag bytes;
 *   - the window record is {x1, x2, y1, y2}: type 1 puts 0x31F at +2 and
 *     0x3FF at +6 (0x00E34C26/0x00E34C2C) and type 2 swaps them
 *     (0x00E34C40/0x00E34C46), with the +0/+4 zeros written later, at
 *     0x00E34C92/0x00E34C96;
 *   - the display unit handed to SMD_$INQ_DISP_TYPE is the constant cell at
 *     0x00E34CEE, not a stack local, and it holds 1;
 *   - the three function-name strings keep their '%' terminator and the
 *     context argument is the one-character "$" cell at 0x00E34D0A;
 *   - `bmi.b 0x00E34CE4` at 0x00E34C64 LEAVES when DTTY_$USE_DTTY is true.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "dtty/dtty_internal.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-46s", #name);                                             \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed) { tests_failed++; } else { printf("PASSED\n"); }   \
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

/* ---- module data the function writes ---------------------------------- */

uint16_t DTTY_$DISP_TYPE;
uint16_t DTTY_$CTRL;
int8_t DTTY_$USE_DTTY;
int8_t DTTY_FLAG_04;
int8_t DTTY_FLAG_06;
void *DTTY_$STD_FONT_P;
uint16_t PROC1_$CURRENT;

/*
 * The two display status registers live at 0x00FC0066 and 0x00FDEBE6.  The
 * host build reaches them through ARCH_VA_TO_PTR, so point ARCH_HOST_VA_BASE
 * at an arena that covers whichever of the two the test needs.
 */
static uint8_t reg_arena[0x80];

/* ---- mocked callees --------------------------------------------------- */

static uint16_t mock_disp_type;
static const uint16_t *inq_unit_arg;
static int inq_calls;

static int assoc_calls;
static uint16_t assoc_unit_value;
static status_$t mock_assoc_status;

static int clear_window_calls;
static smd_rect_t clear_window_rect;
static status_$t mock_clear_window_status;

static int load_font_calls;
static void **load_font_arg;
static status_$t mock_load_font_status;

static int report_calls;
static status_$t report_status;
static const char *report_func;
static const char *report_context;

uint16_t SMD_$INQ_DISP_TYPE(uint16_t *unit)
{
    inq_calls++;
    inq_unit_arg = unit;
    return mock_disp_type;
}

void SMD_$ASSOC(uint16_t *unit, uint16_t *asid, status_$t *status_ret)
{
    (void)asid;
    assoc_calls++;
    assoc_unit_value = *unit;
    *status_ret = mock_assoc_status;
}

void dtty_$clear_window(void *region, status_$t *status_ret)
{
    clear_window_calls++;
    memcpy(&clear_window_rect, region, sizeof(clear_window_rect));
    *status_ret = mock_clear_window_status;
}

void dtty_$load_font(void **font_ptr, status_$t *status_ret)
{
    load_font_calls++;
    load_font_arg = font_ptr;
    *status_ret = mock_load_font_status;
}

void dtty_$report_error(status_$t status, const char *func_name,
                        const char *context)
{
    report_calls++;
    report_status = status;
    report_func = func_name;
    report_context = context;
}

#include "../init.c"

/* ---- fixture ---------------------------------------------------------- */

static void setup(uint32_t reg_addr, uint16_t reg_value)
{
    memset(reg_arena, 0, sizeof(reg_arena));
    /* Map reg_addr (rounded down to the arena base) into reg_arena. */
    ARCH_HOST_VA_BASE = (uintptr_t)reg_arena - (uintptr_t)(reg_addr & ~0x3Fu);
    *(uint16_t *)(reg_arena + (reg_addr & 0x3Fu)) = reg_value;

    DTTY_$DISP_TYPE = 0;
    DTTY_$CTRL = 0;
    DTTY_$USE_DTTY = 0x7F;
    DTTY_FLAG_04 = 0x7F;
    DTTY_FLAG_06 = 0x7F;
    DTTY_$STD_FONT_P = NULL;
    PROC1_$CURRENT = 5;

    mock_disp_type = 1;
    inq_unit_arg = NULL;
    inq_calls = 0;
    assoc_calls = 0;
    assoc_unit_value = 0xFFFF;
    mock_assoc_status = status_$ok;
    clear_window_calls = 0;
    memset(&clear_window_rect, 0x5A, sizeof(clear_window_rect));
    mock_clear_window_status = status_$ok;
    load_font_calls = 0;
    load_font_arg = NULL;
    mock_load_font_status = status_$ok;
    report_calls = 0;
    report_status = 0;
    report_func = NULL;
    report_context = NULL;
}

/* ---- tests ------------------------------------------------------------ */

/* 0x00E34BEE/0x00E34BF2/0x00E34BF6: all three flag bytes are written. */
static void test_module_flags_are_written(void)
{
    int16_t mode = 9;                 /* neither 0 nor 1 -> flag stays false */
    uint16_t ctrl = 0xBEEF;

    setup(DISP_15_STATUS_ADDR, 0);
    mock_disp_type = 0;               /* unknown type -> early return */
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ((int8_t)0xFF, DTTY_$USE_DTTY);
    CHECK_EQ((int8_t)0xFF, DTTY_FLAG_06);
    CHECK_EQ(0, DTTY_FLAG_04);
    CHECK_EQ(0xBEEF, DTTY_$CTRL);
    CHECK_EQ(0, DTTY_$DISP_TYPE);
}

/* The unit is the image cell at 0x00E34CEE and it holds 1. */
static void test_inq_disp_type_gets_the_constant_unit_cell(void)
{
    int16_t mode = 9;
    uint16_t ctrl = 0;

    setup(DISP_15_STATUS_ADDR, 0);
    mock_disp_type = 0;
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ(1, inq_calls);
    CHECK(inq_unit_arg == &dtty_$init_unit);
    CHECK_EQ(1, *inq_unit_arg);
}

/* 15": x2 = 799 at +2 and y2 = 1023 at +6, with x1/y1 cleared last. */
static void test_window_record_type_1(void)
{
    int16_t mode = 9;
    uint16_t ctrl = 0;

    setup(DISP_15_STATUS_ADDR, 0);
    mock_disp_type = DTTY_DISP_TYPE_15_INCH;
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ(1, clear_window_calls);
    CHECK_EQ(0, clear_window_rect.x1);
    CHECK_EQ(0x31F, clear_window_rect.x2);
    CHECK_EQ(0, clear_window_rect.y1);
    CHECK_EQ(0x3FF, clear_window_rect.y2);
}

/* 19": the two maxima swap places. */
static void test_window_record_type_2(void)
{
    int16_t mode = 9;
    uint16_t ctrl = 0;

    setup(DISP_19_STATUS_ADDR, 0);
    mock_disp_type = DTTY_DISP_TYPE_19_INCH;
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ(1, clear_window_calls);
    CHECK_EQ(0, clear_window_rect.x1);
    CHECK_EQ(0x3FF, clear_window_rect.x2);
    CHECK_EQ(0, clear_window_rect.y1);
    CHECK_EQ(0x31F, clear_window_rect.y2);
}

/*
 * 0x00E34C4C-0x00E34C64: mode 1 forces the flag true, and a true flag ENDS
 * the routine before SMD_$ASSOC.
 */
static void test_mode_one_sets_the_flag_and_returns(void)
{
    int16_t mode = 1;
    uint16_t ctrl = 0x1234;

    setup(DISP_15_STATUS_ADDR, 0);
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ((int8_t)0xFF, DTTY_$USE_DTTY);
    CHECK_EQ(0, assoc_calls);
    CHECK_EQ(0, clear_window_calls);
    CHECK_EQ(0x1234, DTTY_$CTRL);   /* the clr.w at 0x00E34C66 is skipped */
}

/* mode 0 with the hardware bit set is equally "true" and equally short. */
static void test_mode_zero_with_hardware_bit(void)
{
    int16_t mode = 0;
    uint16_t ctrl = 0;

    setup(DISP_15_STATUS_ADDR, 1);
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ((int8_t)0xFF, DTTY_$USE_DTTY);
    CHECK_EQ(0, assoc_calls);
}

/* mode 0 with the bit clear is false, so the SMD path runs. */
static void test_mode_zero_without_hardware_bit_runs_smd_path(void)
{
    int16_t mode = 0;
    uint16_t ctrl = 0x4321;

    setup(DISP_15_STATUS_ADDR, 0);
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ(0, DTTY_$USE_DTTY);
    CHECK_EQ(0, DTTY_$CTRL);            /* clr.w (0x2,A2) at 0x00E34C66 */
    CHECK_EQ(1, assoc_calls);
    CHECK_EQ(1, assoc_unit_value);      /* the local set to 1 in both arms */
    CHECK_EQ(1, clear_window_calls);
    CHECK_EQ(1, load_font_calls);
    CHECK(load_font_arg == &DTTY_$STD_FONT_P);
    CHECK_EQ(0, report_calls);
}

/* Each failure reports its own '%'-terminated name and the "$" context. */
static void test_assoc_failure_message(void)
{
    int16_t mode = 9;
    uint16_t ctrl = 0;

    setup(DISP_15_STATUS_ADDR, 0);
    mock_assoc_status = 0x00130001;
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ(1, report_calls);
    CHECK_EQ(0x00130001, report_status);
    CHECK(report_func != NULL && strcmp(report_func, "smd_$assoc%") == 0);
    CHECK(report_context != NULL && strcmp(report_context, "$") == 0);
    CHECK_EQ(0, clear_window_calls);
}

static void test_clear_window_failure_message(void)
{
    int16_t mode = 9;
    uint16_t ctrl = 0;

    setup(DISP_15_STATUS_ADDR, 0);
    mock_clear_window_status = 0x00130002;
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ(1, report_calls);
    CHECK(report_func != NULL &&
          strcmp(report_func, "dtty_$clear_window%") == 0);
    CHECK(report_context != NULL && strcmp(report_context, "$") == 0);
    CHECK_EQ(0, load_font_calls);
}

static void test_load_font_failure_message(void)
{
    int16_t mode = 9;
    uint16_t ctrl = 0;

    setup(DISP_15_STATUS_ADDR, 0);
    mock_load_font_status = 0x0013000B;
    DTTY_$INIT(&mode, &ctrl);

    CHECK_EQ(1, report_calls);
    CHECK(report_func != NULL &&
          strcmp(report_func, "smd_$copy_font_to_md_hdm%") == 0);
    CHECK(report_context != NULL && strcmp(report_context, "$") == 0);
}

int main(void)
{
    printf("DTTY_$INIT tests:\n");
    RUN_TEST(module_flags_are_written);
    RUN_TEST(inq_disp_type_gets_the_constant_unit_cell);
    RUN_TEST(window_record_type_1);
    RUN_TEST(window_record_type_2);
    RUN_TEST(mode_one_sets_the_flag_and_returns);
    RUN_TEST(mode_zero_with_hardware_bit);
    RUN_TEST(mode_zero_without_hardware_bit_runs_smd_path);
    RUN_TEST(assoc_failure_message);
    RUN_TEST(clear_window_failure_message);
    RUN_TEST(load_font_failure_message);
    printf("%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
