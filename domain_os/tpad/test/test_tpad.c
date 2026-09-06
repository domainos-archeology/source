/*
 * tpad/test/test_tpad.c - Unit tests for TPAD subsystem
 *
 * Tests the pointing device coordinate calculations and mode handling.
 */

/*
 * The kernel headers are included before any host header so that the
 * Domain/OS definitions of clock_t, uid_t, true/false, etc. win.
 */
#include "tpad/tpad_internal.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

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

/*
 * These tests only exercise the TPAD type definitions and constants; no
 * tpad/*.c implementation is included, so no SMD/TIME/math mocks are
 * needed here.
 */

/*
 * Test: smd_$pos_t union layout
 */
TEST(pos_layout) {
    smd_$pos_t pos;
    pos.y = 100;
    pos.x = 200;

    /* y occupies bytes 0-1, x occupies bytes 2-3 (matches the m68k layout) */
    int16_t halves[2];
    memcpy(halves, &pos, sizeof(halves));
    ASSERT_EQ(100, pos.y);
    ASSERT_EQ(200, pos.x);
    ASSERT_EQ(100, halves[0]);
    ASSERT_EQ(200, halves[1]);
    ASSERT_EQ(4, sizeof(smd_$pos_t));

    /*
     * The packed 32-bit view is (y << 16) | x only where the host stores
     * the high-order half first, i.e. on big-endian (m68k) hosts.
     */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    ASSERT_EQ((100 << 16) | 200, pos.raw);
#endif
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
