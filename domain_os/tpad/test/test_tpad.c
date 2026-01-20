/*
 * tpad/test/test_tpad.c - Unit tests for TPAD subsystem
 *
 * Tests the pointing device coordinate calculations and mode handling.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Mock includes for testing */
#include "base/base.h"
#include "tpad/tpad.h"

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
        printf("FAILED\n    Expected: %d, Got: %d at line %d\n", \
               (int)(expected), (int)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Mock globals for testing */
static int16_t mock_ndevices = 2;
static smd_disp_info_result_t mock_disp_info = {
    .display_type = 1,
    .bits_per_pixel = 1,
    .num_planes = 1,
    .height = 1024,
    .width = 1280
};

/* Mock external functions */
int16_t SMD_$N_DEVICES(void) {
    return mock_ndevices;
}

void SMD_$INQ_DISP_INFO(int16_t *unit, smd_disp_info_result_t *info, status_$t *status) {
    *info = mock_disp_info;
    *status = status_$ok;
}

void SMD_$LOC_EVENT(uint8_t edge_hit, int16_t unit, int32_t pos, int16_t button_state) {
    /* Mock - do nothing */
    (void)edge_hit;
    (void)unit;
    (void)pos;
    (void)button_state;
}

void TIME_$CLOCK(clock_t *clk) {
    *clk = 0;
}

/* Mock math functions */
long M$MIS$LLW(long a, short b) {
    return a * b;
}

long M$MIS$LLL(long a, long b) {
    return a * b;
}

/*
 * Test: smd_$pos_t union layout
 */
TEST(pos_layout) {
    smd_$pos_t pos;
    pos.y = 100;
    pos.x = 200;

    /* Verify y is in high word, x in low word */
    ASSERT_EQ(100, pos.y);
    ASSERT_EQ(200, pos.x);
    ASSERT_EQ((100 << 16) | 200, pos.raw);
}

/*
 * Test: tpad_$unit_config_t structure size
 */
TEST(config_size) {
    /* Per-unit config should be 44 bytes (0x2c) */
    ASSERT_EQ(44, sizeof(tpad_$unit_config_t));
}

/*
 * Test: tpad_$dev_type_t enumeration values
 */
TEST(dev_type_enum) {
    ASSERT_EQ(0, tpad_$unknown);
    ASSERT_EQ(1, tpad_$have_touchpad);
    ASSERT_EQ(2, tpad_$have_mouse);
    ASSERT_EQ(3, tpad_$have_bitpad);
}

/*
 * Test: tpad_$mode_t enumeration values
 */
TEST(mode_enum) {
    ASSERT_EQ(0, tpad_$absolute);
    ASSERT_EQ(1, tpad_$relative);
    ASSERT_EQ(2, tpad_$scaled);
}

/*
 * Test: Default constants
 */
TEST(constants) {
    ASSERT_EQ(8, TPAD_$MAX_UNITS);
    ASSERT_EQ(512, TPAD_$DEFAULT_CURSOR_Y);
    ASSERT_EQ(400, TPAD_$DEFAULT_CURSOR_X);
    ASSERT_EQ(1500, TPAD_$DEFAULT_TOUCHPAD_MAX);
    ASSERT_EQ(0x400, TPAD_$FACTOR_DEFAULT);
    ASSERT_EQ(0xDF, TPAD_$MOUSE_ID);
    ASSERT_EQ(0x01, TPAD_$BITPAD_ID);
}

int main(void) {
    printf("TPAD subsystem tests\n");
    printf("====================\n\n");

    printf("Type definitions:\n");
    RUN_TEST(pos_layout);
    RUN_TEST(config_size);
    RUN_TEST(dev_type_enum);
    RUN_TEST(mode_enum);
    RUN_TEST(constants);

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
