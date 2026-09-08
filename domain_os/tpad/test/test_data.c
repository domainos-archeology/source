/*
 * tpad/test/test_data.c - Unit tests for TPAD_$DATA (0x00E691BC).
 *
 * The real tpad/data.c is #included below; M$MIS$LLW, M$MIS$LLL and
 * SMD_$LOC_EVENT are mocked.  Every case drives the bitpad arm
 * (dev_id == 0x01) in relative mode, which is the shortest route into the
 * acceleration block at 0x00E6961C-0x00E69684.
 *
 * Facts under test (bead source-0ni8):
 *   - `cmpi.w #0x64,D5w / ble.b 0x00E69664` accelerates only when the
 *     velocity is STRICTLY GREATER than 100;
 *   - the time factor is the difference of the LOW 32 BITS of the two
 *     48-bit clocks - `move.l (0x6,A1),D3` over the packet clock at +4 and
 *     `sub.l (0x166,A5),D3` over tpad_$last_clock at 0x164 - not of their
 *     high longwords;
 *   - `move.l (0x4,A1),(0x164,A5)` and `move.w (0x8,A1),(0x168,A5)` store
 *     all six clock bytes;
 *   - 0x00E6967A `clr.w D1w` falls through into 0x00E6967C `st D4b`, so the
 *     snap-to-horizontal arm raises the same flag the acceleration arm does.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "tpad/tpad_internal.h"

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
            printf("      %s:%d: %s: expected %ld, got %ld\n", __FILE__,      \
                   __LINE__, #actual, _e, _a);                                \
        }                                                                     \
    } while (0)

/* ---- mocked globals --------------------------------------------------- */

tpad_$unit_config_t tpad_$unit_configs[TPAD_$MAX_UNITS];
tpad_$globals_t tpad_$globals;

/* ---- mocked callees --------------------------------------------------- */

long M$MIS$LLW(long multiplicand, short multiplier)
{
    return multiplicand * (long)multiplier;
}

long M$MIS$LLL(long multiplicand, long multiplier)
{
    return multiplicand * multiplier;
}

static int loc_event_calls;
static int8_t loc_event_type[4];
static uint32_t loc_event_pos[4];
static int16_t loc_event_buttons[4];

int8_t SMD_$LOC_EVENT(int8_t event_type, int16_t unit, uint32_t pos,
                      int16_t buttons)
{
    (void)unit;
    if (loc_event_calls < 4) {
        loc_event_type[loc_event_calls] = event_type;
        loc_event_pos[loc_event_calls] = pos;
        loc_event_buttons[loc_event_calls] = buttons;
    }
    loc_event_calls++;
    return 0;
}

#include "../data.c"

/* ---- fixture ---------------------------------------------------------- */

static tpad_$unit_config_t *cfg;

/*
 * A bitpad packet whose scaled raw coordinates both come out zero: with
 * x_scale == y_scale == 0 the bitpad arm computes
 *   raw_x = (raw * 0) / 0x898 = 0
 *   raw_y = 0 - (raw * 0) / 0x898 = 0
 * so the deltas are exactly the cursor offsets.
 */
static void setup(int16_t offset_x, int16_t offset_y, int16_t hysteresis)
{
    memset(tpad_$unit_configs, 0, sizeof(tpad_$unit_configs));
    memset(&tpad_$globals, 0, sizeof(tpad_$globals));

    tpad_$unit = 1;
    cfg = TPAD_$UNIT_CONFIG(1);

    cfg->mode = tpad_$relative;
    cfg->x_scale = 0;
    cfg->y_scale = 0;
    cfg->x_factor = 1;
    cfg->y_factor = 1;
    cfg->hysteresis = hysteresis;
    cfg->cursor_offset_x = offset_x;
    cfg->cursor_offset_y = offset_y;
    cfg->x_min_disp = -1000;
    cfg->x_max_disp = 1000;
    cfg->y_min_disp = -1000;
    cfg->y_max_disp = 1000;
    cfg->punch_impact = 0x7FFF;

    /* Already a bitpad, so device_changed is false and the re-origin block
     * at 0x00E695E2 is skipped (elapsed stays 0 as well). */
    tpad_$dev_type = tpad_$have_bitpad;
    tpad_$cursor_x = 0;
    tpad_$cursor_y = 0;

    loc_event_calls = 0;
}

static tpad_$data_packet_t make_packet(uint32_t clock_high, uint16_t clock_low)
{
    tpad_$data_packet_t p;
    memset(&p, 0, sizeof(p));
    p.elapsed = 0;
    p.clock_high = clock_high;
    p.clock_low = clock_low;
    p.dev_id = TPAD_$BITPAD_ID;
    return p;
}

/* ---- tests ------------------------------------------------------------ */

/*
 * velocity == 100 is NOT accelerated: `ble` takes the small-movement arm.
 * With hysteresis 1 the cursor lands on delta - 1 and, because the flag
 * stays false, the offsets are left alone (0x00E6973A `bpl`).
 */
static void test_velocity_exactly_100_is_not_accelerated(void)
{
    tpad_$data_packet_t p = make_packet(0, 0);

    setup(100, 0, 1);
    TPAD_$DATA(&p);

    CHECK_EQ(99, tpad_$cursor_x);
    CHECK_EQ(100, cfg->cursor_offset_x);   /* not rewritten */
    CHECK_EQ(0, cfg->cursor_offset_y);
}

/*
 * velocity == 101 IS accelerated.  The clocks are equal here, so the time
 * factor is 0, the velocity keeps its value and the multiplier is
 * 101/100 + 1 = 2: delta_x becomes 202 and the cursor lands on 201.
 */
static void test_velocity_101_accelerates(void)
{
    tpad_$data_packet_t p = make_packet(0, 0);

    setup(101, 0, 1);
    TPAD_$DATA(&p);

    CHECK_EQ(201, tpad_$cursor_x);
    CHECK_EQ(201, cfg->cursor_offset_x);   /* rewritten: the flag is true */
}

/*
 * The time factor comes from the LOW 32 bits of the 48-bit clocks.  A packet
 * clock of high=1, low=0 is 0x00010000 = 65536 ticks past a zero
 * tpad_$last_clock, so the factor is 65536/9000 = 7, the velocity becomes
 * 101/7 = 14 and the multiplier is 14/100 + 1 = 1: delta_x stays 101 and the
 * cursor lands on 100.
 *
 * Under the old "difference of the high longwords" reading the difference
 * would be 1, the factor 0, and the cursor would land on 201 instead.
 */
static void test_clock_delta_uses_the_low_32_bits(void)
{
    tpad_$data_packet_t p = make_packet(1, 0);

    setup(101, 0, 1);
    TPAD_$DATA(&p);

    CHECK_EQ(100, tpad_$cursor_x);
}

/* The same packet with a matching last_clock gives a zero difference. */
static void test_clock_delta_is_zero_when_clocks_match(void)
{
    tpad_$data_packet_t p = make_packet(1, 0);

    setup(101, 0, 1);
    tpad_$last_clock.high = 1;
    tpad_$last_clock.low = 0;
    TPAD_$DATA(&p);

    CHECK_EQ(201, tpad_$cursor_x);
}

/* All six clock bytes are copied into tpad_$last_clock. */
static void test_clock_is_stored_whole(void)
{
    tpad_$data_packet_t p = make_packet(0x00011234u, 0xABCDu);

    setup(101, 0, 1);
    TPAD_$DATA(&p);

    CHECK_EQ(0x00011234u, tpad_$last_clock.high);
    CHECK_EQ(0xABCDu, tpad_$last_clock.low);
}

/*
 * The snap-to-horizontal arm: velocity 66 (<= 100) with |dx/dy| = 10 > 5
 * zeroes delta_y AND falls through into `st D4b`, so the offsets are
 * rewritten even though no edge was hit.
 */
static void test_snap_to_horizontal_sets_the_flag(void)
{
    tpad_$data_packet_t p = make_packet(0, 0);

    setup(60, 6, 2);
    TPAD_$DATA(&p);

    CHECK_EQ(58, tpad_$cursor_x);          /* 60 - 2 */
    CHECK_EQ(0, tpad_$cursor_y);           /* delta_y was zeroed */
    CHECK_EQ(58, cfg->cursor_offset_x);    /* rewritten -> the flag was set */
    CHECK_EQ(0, cfg->cursor_offset_y);
}

/* A ratio of 5 or less leaves both the delta and the flag alone. */
static void test_small_ratio_leaves_the_flag_clear(void)
{
    tpad_$data_packet_t p = make_packet(0, 0);

    setup(10, 6, 2);
    TPAD_$DATA(&p);

    CHECK_EQ(8, tpad_$cursor_x);           /* 10 - 2 */
    CHECK_EQ(4, tpad_$cursor_y);           /* 6 - 2, delta_y survived */
    CHECK_EQ(10, cfg->cursor_offset_x);    /* NOT rewritten */
    CHECK_EQ(6, cfg->cursor_offset_y);
}

/*
 * The routine always ends with the ordinary locator event, packing Y in the
 * high half of the position longword, and clears the packet's elapsed field.
 */
static void test_locator_event_and_packet_clear(void)
{
    tpad_$data_packet_t p = make_packet(0, 0);

    setup(10, 6, 0);
    p.flags = 0x3C;                        /* buttons = (0x3C & 0x3C) >> 2 */
    TPAD_$DATA(&p);

    CHECK_EQ(0u, p.elapsed);
    CHECK_EQ(1, loc_event_calls);
    CHECK_EQ(0, loc_event_type[0]);
    CHECK_EQ(0x000F, loc_event_buttons[0]);
    CHECK_EQ(((uint32_t)(uint16_t)tpad_$cursor_y << 16) |
                 (uint32_t)(uint16_t)tpad_$cursor_x,
             loc_event_pos[0]);
}

int main(void)
{
    printf("TPAD_$DATA tests:\n");
    RUN_TEST(velocity_exactly_100_is_not_accelerated);
    RUN_TEST(velocity_101_accelerates);
    RUN_TEST(clock_delta_uses_the_low_32_bits);
    RUN_TEST(clock_delta_is_zero_when_clocks_match);
    RUN_TEST(clock_is_stored_whole);
    RUN_TEST(snap_to_horizontal_sets_the_flag);
    RUN_TEST(small_ratio_leaves_the_flag_clear);
    RUN_TEST(locator_event_and_packet_clear);
    printf("%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
