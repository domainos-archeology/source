/*
 * smd/test/test_show_cursor.c - Unit tests for SHOW_CURSOR (0x00E6E1CC)
 *
 * The real smd/show_cursor.c is #included below and the real SHOW_CURSOR is
 * called; everything it depends on is mocked in this translation unit.
 *
 * The interesting logic is the cursor bounding box computation and the
 * tracking-rectangle overlap test, neither of which is directly observable.
 * Both are probed indirectly: a tracking rectangle is placed so that the
 * overlap test only succeeds when the box has been clipped exactly the way
 * the assembly clips it, and the resulting visibility is then read back out
 * of the recorded SMD_$XOR_CURSOR calls / the cursor_pending_flag.
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
            printf("      %s:%d: %s: expected %ld, got %ld\n", __FILE__,      \
                   __LINE__, #actual, _e, _a);                                \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Layout checks on the structures SHOW_CURSOR relies on.              */
/*                                                                     */
/* smd_globals_t contains no pointers, so its m68k layout is also its  */
/* host layout and these can be checked unconditionally here (the      */
/* header's own copies are guarded by ARCH_M68K).                      */
/* ------------------------------------------------------------------ */

_Static_assert(offsetof(smd_globals_t, asid_to_unit) == 0x48, "asid_to_unit");
_Static_assert(offsetof(smd_globals_t, kbd_cursor_track_rect) == 0xC0, "kbd");
_Static_assert(offsetof(smd_globals_t, blank_time) == 0xC8, "blank_time");
_Static_assert(offsetof(smd_globals_t, saved_cursor_pos) == 0xCC, "saved_pos");
_Static_assert(offsetof(smd_globals_t, default_cursor_pos) == 0xD0, "def_pos");
_Static_assert(offsetof(smd_globals_t, cursor_button_state) == 0xD4, "cbs");
_Static_assert(offsetof(smd_globals_t, last_button_state) == 0xD6, "lbs");
_Static_assert(offsetof(smd_globals_t, blank_timeout) == 0xD8, "blank_to");
_Static_assert(offsetof(smd_globals_t, blank_enabled) == 0xDC, "blank_en");
_Static_assert(offsetof(smd_globals_t, blank_pending) == 0xDD, "blank_pend");
_Static_assert(offsetof(smd_globals_t, tp_reporting) == 0xDE, "tp_rep");
_Static_assert(offsetof(smd_globals_t, tracking_enabled) == 0xE0, "trk_en");
_Static_assert(offsetof(smd_globals_t, tp_cursor_timeout) == 0xE2, "tp_to");
_Static_assert(offsetof(smd_globals_t, tracking_cursor_num) == 0xE4, "trk_csr");
_Static_assert(offsetof(smd_globals_t, tracking_rect_count) == 0xE6, "trk_cnt");
_Static_assert(offsetof(smd_globals_t, tracking_rects) == 0xE8, "trk_rects");
_Static_assert(offsetof(smd_globals_t, event_queue_head) == 0x728, "eq_head");
_Static_assert(offsetof(smd_globals_t, event_queue_tail) == 0x72A, "eq_tail");
_Static_assert(offsetof(smd_globals_t, event_queue) == 0x72C, "eq");
_Static_assert(offsetof(smd_globals_t, cursor_pending_flag) == 0x1744, "cpf");
_Static_assert(offsetof(smd_globals_t, request_queue_tail) == 0x17F0, "rq_t");
_Static_assert(offsetof(smd_globals_t, request_queue_head) == 0x17F2, "rq_h");
_Static_assert(offsetof(smd_globals_t, request_queue) == 0x17F4, "rq");
_Static_assert(offsetof(smd_globals_t, cursor_pos_sentinel) == 0x1D94, "sent");
_Static_assert(offsetof(smd_globals_t, default_unit) == 0x1D98, "def_unit");
_Static_assert(offsetof(smd_globals_t, response_pending) == 0x1D9A, "resp");
_Static_assert(offsetof(smd_globals_t, previous_unit) == 0x1D9C, "prev_unit");
_Static_assert(offsetof(smd_globals_t, unit_change_count) == 0x1D9E, "ucc");
_Static_assert(offsetof(smd_globals_t, last_idm_button) == 0x1DA0, "idm");
_Static_assert(offsetof(smd_globals_t, power_off_reported) == 0x1DA2, "poff");
/* 0x1DA0 is also blink_func[0]; the record runs to 0x1DA8 because
 * SMD_$BLINK_CURSOR_CALLBACK reads blink_func[1] at SMD_GLOBALS + 0x1DA4
 * (0x00E84930 = 0x00E2722C = SMD_$BLINK_CURSOR_1) and the code at
 * SMD_GLOBALS + 0x1DA8 is a trampoline.  Bead source-q85g. */
_Static_assert(offsetof(smd_globals_t, blink_func) == 0x1DA0, "blink");
_Static_assert(sizeof(smd_globals_t) == 0x1DA8, "smd_globals_t size");
_Static_assert(sizeof(smd_track_rect_t) == 8, "smd_track_rect_t size");
_Static_assert(sizeof(smd_request_entry_t) == 36, "smd_request_entry_t size");

/* Cursor pattern layout is pointer-free too. */
_Static_assert(offsetof(smd_cursor_pattern_t, width) == 0x00, "pat width");
_Static_assert(offsetof(smd_cursor_pattern_t, height) == 0x02, "pat height");
_Static_assert(offsetof(smd_cursor_pattern_t, hot_x) == 0x04, "pat hot_x");
_Static_assert(offsetof(smd_cursor_pattern_t, hot_y_adj) == 0x06, "pat hot_y");
_Static_assert(offsetof(smd_cursor_pattern_t, bitmap) == 0x08, "pat bitmap");

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
/* Big enough that smd_$unit_rec(1) (base + 0x10C - 0xF4) plus a whole
 * record stays inside the object on a 64-bit host too. */
uint8_t SMD_DISPLAY_UNITS[SMD_MAX_DISPLAY_UNITS * SMD_DISPLAY_UNIT_SIZE + 0x18];
smd_display_info_t SMD_DISPLAY_INFO[SMD_DISPLAY_INFO_COUNT];
smd_time_com_t SMD_TIME_$COM;
ml_$exclusion_t ml_$exclusion_t_00e2e520;
uint16_t PROC1_$AS_ID;

static smd_display_hw_t test_hw;
static smd_cursor_pattern_t test_patterns[4];
smd_cursor_pattern_t *SMD_CURSOR_PTABLE[4];

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int8_t mock_validate_result;
static int excl_start_calls, excl_stop_calls;
static int acq_calls, rel_calls, lock_calls;
static int8_t mock_lock_result;
static boolean mock_draw_result;

typedef struct {
    int16_t cursor_num;
    uint32_t cursor_pos;
    int16_t *bounds;
    smd_display_hw_t *hw;
    boolean erase_flag;
    uint32_t display_base;
    SMD_HW_REG_PTR ctrl_regs;
} draw_call_t;

static draw_call_t draw_calls[4];
static int draw_call_count;

int8_t smd_$validate_unit(uint16_t unit)
{
    (void)unit;
    return mock_validate_result;
}

void ML_$EXCLUSION_START(ml_$exclusion_t *excl)
{
    CHECK(excl == &ml_$exclusion_t_00e2e520);
    excl_start_calls++;
}

void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)
{
    CHECK(excl == &ml_$exclusion_t_00e2e520);
    excl_stop_calls++;
}

uint16_t SMD_$ACQ_DISPLAY(int16_t *lock_data)
{
    CHECK_EQ(1, *lock_data);
    acq_calls++;
    return 0;
}

int8_t SMD_$LOCK_DISPLAY(smd_display_hw_t *hw, int16_t *lock_data)
{
    (void)hw;
    CHECK_EQ(1, *lock_data);
    lock_calls++;
    return mock_lock_result;
}

void SMD_$REL_DISPLAY(void) { rel_calls++; }

boolean SMD_$XOR_CURSOR(int16_t *cursor_num, uint32_t *cursor_pos,
                                  int16_t *bounds, smd_display_hw_t *hw,
                                  const boolean *erase_flag,
                                  uint32_t display_base,
                                  SMD_HW_REG_PTR ctrl_regs)
{
    if (draw_call_count < (int)(sizeof(draw_calls) / sizeof(draw_calls[0]))) {
        draw_call_t *c = &draw_calls[draw_call_count];
        c->cursor_num = *cursor_num;
        c->cursor_pos = *cursor_pos;
        c->bounds = bounds;
        c->hw = hw;
        c->erase_flag = *erase_flag;
        c->display_base = display_base;
        c->ctrl_regs = ctrl_regs;
    }
    draw_call_count++;
    return mock_draw_result;
}

/* The implementation under test. */
#include "smd/show_cursor.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

#define TEST_UNIT 1

static smd_display_unit_t *test_rec(void) { return smd_$unit_rec(TEST_UNIT); }

/*
 * Reset everything to a state where SHOW_CURSOR gets all the way to the
 * draw call: unit valid, previous unit == default unit, nothing on screen.
 */
static void setup(int16_t max_x, int16_t max_y)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(SMD_DISPLAY_UNITS, 0, sizeof(SMD_DISPLAY_UNITS));
    memset(&test_hw, 0, sizeof(test_hw));
    memset(&SMD_TIME_$COM, 0, sizeof(SMD_TIME_$COM));
    memset(test_patterns, 0, sizeof(test_patterns));
    memset(draw_calls, 0, sizeof(draw_calls));
    draw_call_count = 0;
    excl_start_calls = excl_stop_calls = 0;
    acq_calls = rel_calls = lock_calls = 0;

    mock_validate_result = (int8_t)0xFF; /* valid */
    mock_lock_result = -1;               /* lock acquired */
    mock_draw_result = true;             /* draw succeeded */

    PROC1_$AS_ID = 3;

    test_hw.max_x = max_x;
    test_hw.max_y = max_y;
    test_hw.cursor_visible = false;
    test_hw.cursor_number = 0;
    test_hw.cursor_pos = SMD_POS_MAKE(0, 0);

    test_rec()->hw = &test_hw;
    test_rec()->display_base = 0x00FC0000u;
    test_rec()->ctrl_regs = (SMD_HW_REG_PTR)0;

    for (int i = 0; i < 4; i++) {
        SMD_CURSOR_PTABLE[i] = &test_patterns[i];
    }
    /* Cursor 0: 16x16 with the hot spot at its top-left. */
    test_patterns[0].width = 16;
    test_patterns[0].height = 16;
    test_patterns[0].hot_x = 0;
    test_patterns[0].hot_y_adj = 15;

    SMD_GLOBALS.default_unit = TEST_UNIT;
    SMD_GLOBALS.previous_unit = TEST_UNIT;
    SMD_GLOBALS.cursor_pos_sentinel = 0x80000000u;
    SMD_GLOBALS.default_cursor_pos = 0xDEADBEEFu; /* != anything we pass */
    SMD_GLOBALS.cursor_button_state = 0;
    SMD_GLOBALS.cursor_pending_flag = false;
    SMD_GLOBALS.tracking_rect_count = 0;
}

static void add_rect(int16_t x1, int16_t x2, int16_t y1, int16_t y2)
{
    smd_track_rect_t *r =
        &SMD_GLOBALS.tracking_rects[SMD_GLOBALS.tracking_rect_count++];
    r->x1 = x1;
    r->x2 = x2;
    r->y1 = y1;
    r->y2 = y2;
}

static void call_show(uint32_t pos, int16_t cursor_num, boolean blocking)
{
    SHOW_CURSOR(&pos, &cursor_num, &blocking);
}

/*
 * "Was the cursor considered visible?"  SHOW_CURSOR only calls
 * SMD_$XOR_CURSOR for the new cursor when `visible` is true, and
 * on a successful draw it clears cursor_pending_flag; when `visible` is
 * false it never draws.
 */
static int cursor_was_drawn(void) { return draw_call_count > 0; }

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_invalid_unit_returns_immediately(void)
{
    setup(1023, 799);
    mock_validate_result = 0; /* not valid */
    call_show(SMD_POS_MAKE(10, 10), 0, true);

    CHECK_EQ(0, excl_start_calls);
    CHECK_EQ(0, acq_calls);
    CHECK_EQ(0, draw_call_count);
    /* previous_unit must not have been touched */
    CHECK_EQ(TEST_UNIT, SMD_GLOBALS.previous_unit);
}

static void test_previous_unit_defaults_when_minus_one(void)
{
    setup(1023, 799);
    SMD_GLOBALS.previous_unit = -1;
    call_show(SMD_POS_MAKE(10, 10), 0, true);
    CHECK_EQ(TEST_UNIT, SMD_GLOBALS.previous_unit);
}

static void test_sentinel_position_uses_hw_position(void)
{
    setup(1023, 799);
    test_hw.cursor_pos = SMD_POS_MAKE(77, 88);
    call_show(SMD_GLOBALS.cursor_pos_sentinel, 0, true);
    /* 00e6e250 move.l (0x32,A2),(-0x8,A6), then 00e6e324 writes it back */
    CHECK_EQ(SMD_POS_MAKE(77, 88), test_hw.cursor_pos);
}

static void test_cursor_num_minus_one_uses_hw_cursor(void)
{
    setup(1023, 799);
    test_hw.cursor_number = 2;
    test_patterns[2] = test_patterns[0];
    call_show(SMD_POS_MAKE(10, 10), -1, true);
    /* 00e6e25c move.w (0x36,A2),D6w; 00e6e320 stores it straight back */
    CHECK_EQ(2, test_hw.cursor_number);
}

static void test_exclusion_bracket_always_balanced(void)
{
    setup(1023, 799);
    add_rect(0, 10, 0, 10);
    call_show(SMD_POS_MAKE(500, 500), 0, true);
    CHECK_EQ(1, excl_start_calls);
    CHECK_EQ(1, excl_stop_calls);
}

static void test_rect_overlap_hides_cursor(void)
{
    setup(1023, 799);
    /* pos (100,200), 16x16, hot_y_adj 15:
     *   x_left 100, x_right 116, y_bottom 215, y_top 199 */
    add_rect(110, 120, 210, 220);
    call_show(SMD_POS_MAKE(100, 200), 0, true);
    CHECK_EQ(0, cursor_was_drawn());
}

static void test_rect_miss_leaves_cursor_visible(void)
{
    setup(1023, 799);
    add_rect(200, 210, 210, 220); /* x_right 116 is not > 200 */
    call_show(SMD_POS_MAKE(100, 200), 0, true);
    CHECK_EQ(1, cursor_was_drawn());
}

static void test_second_rect_is_scanned(void)
{
    setup(1023, 799);
    add_rect(200, 210, 210, 220);  /* miss */
    add_rect(110, 120, 210, 220);  /* hit  */
    call_show(SMD_POS_MAKE(100, 200), 0, true);
    CHECK_EQ(0, cursor_was_drawn());
}

static void test_left_edge_is_clamped_to_zero(void)
{
    setup(1023, 799);
    test_patterns[0].hot_x = 5; /* x_left would be -5 without the clamp */
    /* The rectangle only reaches x = -1, so it can only be hit by an
     * unclamped x_left. */
    add_rect(-100, -1, 0, 1000);
    call_show(SMD_POS_MAKE(0, 200), 0, true);
    CHECK_EQ(1, cursor_was_drawn()); /* clamped -> no overlap */
}

static void test_right_edge_clamp_shifts_the_box_left(void)
{
    setup(50, 799);
    /* x_left 60, x_right 76 > 50, so x_right becomes 51 and x_left 35. */
    add_rect(0, 40, 0, 1000);
    call_show(SMD_POS_MAKE(60, 200), 0, true);
    CHECK_EQ(0, cursor_was_drawn()); /* 35 <= 40 -> overlap */
}

static void test_right_edge_unclamped_box_would_miss(void)
{
    setup(1023, 799); /* no clamping this time */
    add_rect(0, 40, 0, 1000);
    call_show(SMD_POS_MAKE(60, 200), 0, true);
    CHECK_EQ(1, cursor_was_drawn()); /* x_left 60 > 40 -> no overlap */
}

static void test_bottom_edge_is_clamped_to_max_y(void)
{
    setup(1023, 100);
    /* y_bottom 215 -> 100, y_top 100 - 16 = 84. */
    add_rect(0, 1000, 90, 95);
    call_show(SMD_POS_MAKE(100, 200), 0, true);
    CHECK_EQ(0, cursor_was_drawn()); /* 100 >= 90 and 84 < 95 */
}

static void test_top_edge_is_clamped_to_minus_one(void)
{
    setup(1023, 799);
    test_patterns[0].hot_y_adj = 0; /* y_bottom = 5, y_top = -11 -> -1/15 */
    add_rect(0, 1000, 10, 20);
    call_show(SMD_POS_MAKE(100, 5), 0, true);
    CHECK_EQ(0, cursor_was_drawn()); /* y_bottom becomes 15 >= 10 */
}

static void test_no_change_skips_the_display_update(void)
{
    setup(1023, 799);
    /* Make everything already match: visible, same unit, same pos/cursor. */
    test_hw.cursor_visible = true;
    SMD_GLOBALS.default_cursor_pos = SMD_POS_MAKE(100, 200);
    SMD_GLOBALS.cursor_button_state = 0;
    call_show(SMD_POS_MAKE(100, 200), 0, true);

    CHECK_EQ(0, acq_calls);
    CHECK_EQ(0, lock_calls);
    CHECK_EQ(0, rel_calls);
    CHECK_EQ(0, draw_call_count);
    /* The exclusion bracket still ran, and the hw state was still stored. */
    CHECK_EQ(1, excl_start_calls);
    CHECK_EQ(SMD_POS_MAKE(100, 200), test_hw.cursor_pos);
}

static void test_nonblocking_lock_failure_returns_early(void)
{
    setup(1023, 799);
    mock_lock_result = 0; /* >= 0 == failure */
    call_show(SMD_POS_MAKE(100, 200), 0, false);

    CHECK_EQ(1, lock_calls);
    CHECK_EQ(0, acq_calls);
    CHECK_EQ(0, rel_calls);
    CHECK_EQ(0, draw_call_count);
}

static void test_blocking_uses_acq_display(void)
{
    setup(1023, 799);
    call_show(SMD_POS_MAKE(100, 200), 0, true);
    CHECK_EQ(1, acq_calls);
    CHECK_EQ(0, lock_calls);
    CHECK_EQ(1, rel_calls);
}

static void test_asid_map_updated_before_locking(void)
{
    setup(1023, 799);
    PROC1_$AS_ID = 7;
    call_show(SMD_POS_MAKE(100, 200), 0, true);
    CHECK_EQ(TEST_UNIT, SMD_GLOBALS.asid_to_unit[7]);
}

static void test_erase_then_draw_passes_record_values(void)
{
    setup(1023, 799);
    test_hw.cursor_visible = true;             /* forces the erase call */
    SMD_TIME_$COM.cursor_painted = true; /* and lets it happen */
    test_rec()->display_base = 0x00FC0000u;
    test_rec()->ctrl_regs = (SMD_HW_REG_PTR)&test_hw;
    SMD_GLOBALS.default_cursor_pos = SMD_POS_MAKE(1, 2);
    SMD_GLOBALS.cursor_button_state = 3;
    test_patterns[3] = test_patterns[0];

    call_show(SMD_POS_MAKE(100, 200), 0, true);

    CHECK_EQ(2, draw_call_count);
    /* Erase: the *globals* are handed over by address, with the 0xFF flag. */
    CHECK_EQ(3, draw_calls[0].cursor_num);
    CHECK_EQ(SMD_POS_MAKE(1, 2), draw_calls[0].cursor_pos);
    CHECK_EQ((int8_t)0xFF, draw_calls[0].erase_flag);
    CHECK(draw_calls[0].bounds == &test_hw.min_x);
    CHECK(draw_calls[0].hw == &test_hw);
    /* Draw: the hw's own remembered cursor, with the 0x00 flag. */
    CHECK_EQ(0, draw_calls[1].erase_flag);
    CHECK(draw_calls[1].hw == &test_hw);
    /* Both got the unit record's two longwords, by value. */
    for (int i = 0; i < 2; i++) {
        CHECK_EQ(0x00FC0000u, draw_calls[i].display_base);
        CHECK(draw_calls[i].ctrl_regs == (SMD_HW_REG_PTR)&test_hw);
    }
    /* 00e6e3be clr.b (0x38,A2) after a successful erase, then 00e6e428 st */
    CHECK_EQ((int8_t)0xFF, test_hw.cursor_visible);
    /* 00e6e41a clr.b (0x1744,A5) on a successful draw */
    CHECK_EQ(0, SMD_GLOBALS.cursor_pending_flag);
    /* 00e6e434/00e6e438 */
    CHECK_EQ((int8_t)0xFF, SMD_TIME_$COM.cursor_painted);
    CHECK_EQ(7, SMD_TIME_$COM.blink_defer);
}

static void test_failed_draw_sets_pending_flag(void)
{
    setup(1023, 799);
    mock_draw_result = false; /* 00e6e416 tst.b D0b / bpl -> 0x00e6e440 */
    call_show(SMD_POS_MAKE(100, 200), 0, true);
    CHECK_EQ(1, draw_call_count);
    CHECK_EQ((int8_t)0xFF, SMD_GLOBALS.cursor_pending_flag);
    CHECK_EQ(1, rel_calls);
}

static void test_hidden_cursor_at_drawn_position_sets_pending(void)
{
    setup(1023, 799);
    /* Same position as what is on screen, but hidden by a tracking rect:
     * 00e6e3c2..00e6e3d4 or in (~visible & same_position). */
    SMD_GLOBALS.default_cursor_pos = SMD_POS_MAKE(100, 200);
    SMD_GLOBALS.cursor_button_state = 1; /* differs, so we do not early-out */
    test_patterns[1] = test_patterns[0];
    add_rect(110, 120, 210, 220);
    call_show(SMD_POS_MAKE(100, 200), 0, true);

    CHECK_EQ(0, draw_call_count);
    CHECK_EQ((int8_t)0xFF, SMD_GLOBALS.cursor_pending_flag);
}

int main(void)
{
    printf("SHOW_CURSOR (0x00E6E1CC) tests\n");

    RUN_TEST(invalid_unit_returns_immediately);
    RUN_TEST(previous_unit_defaults_when_minus_one);
    RUN_TEST(sentinel_position_uses_hw_position);
    RUN_TEST(cursor_num_minus_one_uses_hw_cursor);
    RUN_TEST(exclusion_bracket_always_balanced);
    RUN_TEST(rect_overlap_hides_cursor);
    RUN_TEST(rect_miss_leaves_cursor_visible);
    RUN_TEST(second_rect_is_scanned);
    RUN_TEST(left_edge_is_clamped_to_zero);
    RUN_TEST(right_edge_clamp_shifts_the_box_left);
    RUN_TEST(right_edge_unclamped_box_would_miss);
    RUN_TEST(bottom_edge_is_clamped_to_max_y);
    RUN_TEST(top_edge_is_clamped_to_minus_one);
    RUN_TEST(no_change_skips_the_display_update);
    RUN_TEST(nonblocking_lock_failure_returns_early);
    RUN_TEST(blocking_uses_acq_display);
    RUN_TEST(asid_map_updated_before_locking);
    RUN_TEST(erase_then_draw_passes_record_values);
    RUN_TEST(failed_draw_sets_pending_flag);
    RUN_TEST(hidden_cursor_at_drawn_position_sets_pending);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
