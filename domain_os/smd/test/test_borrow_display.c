/*
 * smd/test/test_borrow_display.c - Unit tests for SMD_$BORROW_DISPLAY
 * (0x00E6F584).
 *
 * The real smd/borrow_display.c is #included below; everything it calls is
 * mocked here.
 *
 * Facts under test (bead source-xqln):
 *   - `tst.b D0b / bpl.b 0x00E6F5BE` at 0x00E6F5AA-0x00E6F5AC takes the
 *     ERROR arm when smd_$validate_unit returns a NON-negative byte, and
 *     only a negative (true) result goes on to SMD_$INQ_DISP_TYPE;
 *   - a zero display type reaches the same error arm via `seq D2b / bpl`
 *     (0x00E6F5B8-0x00E6F5BC);
 *   - the wait is EC_$WAIT (0x00E20610) with two 3-element arrays passed by
 *     value: { &hw->cursor_ec, 0, 0 } and { count + 1, 0, 0 }
 *     (0x00E6F62C-0x00E6F644);
 *   - a non-negative response_pending byte denies the borrow
 *     (0x00E6F650 `tst.b (0x1d99,A1)` / `bmi` continues);
 *   - the crash cell is the constant longword 0x0013000E at 0x00E6F6FC.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "smd/smd_internal.h"
#include "ec/ec.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-48s", #name);                                             \
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

/* ---- mocked globals -------------------------------------------------- */

smd_globals_t SMD_GLOBALS;
smd_$wired_data_t SMD_$WIRED_DATA;
uint16_t PROC1_$AS_ID;
/* The borrow eventcount is SMD_$WIRED_DATA.ec_2; nothing else to define. */

/* ---- mocked callees --------------------------------------------------- */

static int8_t mock_validate_result;
static int mock_validate_calls;
static uint16_t mock_disp_type;
static int mock_disp_type_calls;
static int lock_calls, unlock_calls;
static int advance_calls;
static int wait_calls;
static ec_$wait_ecs_t wait_ecs;
static ec_$wait_vals_t wait_vals;
static int init_state_calls;
static int8_t init_state_options;
static status_$t mock_init_state_status;
static int clear_kbd_calls;
static int reset_globals_calls;
static int16_t reset_globals_unit;
static boolean reset_globals_full;
static int clear_window_calls;
static smd_rect_t *clear_window_rect;
static status_$t mock_clear_window_status;
static int crash_calls;
static const status_$t *crash_arg;

int8_t smd_$validate_unit(uint16_t unit)
{
    (void)unit;
    mock_validate_calls++;
    return mock_validate_result;
}

uint16_t SMD_$INQ_DISP_TYPE(uint16_t *unit)
{
    (void)unit;
    mock_disp_type_calls++;
    return mock_disp_type;
}

void ML_$LOCK(int16_t id) { (void)id; lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlock_calls++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advance_calls++; }

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    wait_ecs = ecs;
    wait_vals = vals;
    wait_calls++;
    return 0;
}

void smd_$init_display_state(int8_t options, status_$t *status_ret)
{
    init_state_calls++;
    init_state_options = options;
    *status_ret = mock_init_state_status;
}

void SMD_$CLEAR_KBD_CURSOR(status_$t *status_ret)
{
    (void)status_ret;
    clear_kbd_calls++;
}

void smd_$reset_display_globals(int16_t unit, boolean full)
{
    reset_globals_calls++;
    reset_globals_unit = unit;
    reset_globals_full = full;
}

void SMD_$CLEAR_WINDOW(smd_rect_t *rect, status_$t *status_ret)
{
    clear_window_calls++;
    clear_window_rect = rect;
    *status_ret = mock_clear_window_status;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_arg = status_p;
}

#include "../borrow_display.c"

/* ---- fixture ---------------------------------------------------------- */

#define TEST_UNIT 1
#define TEST_ASID 4

static smd_display_hw_t test_hw;

static void setup(void)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(&SMD_$WIRED_DATA, 0, sizeof(SMD_$WIRED_DATA));
    memset(&test_hw, 0, sizeof(test_hw));

    PROC1_$AS_ID = TEST_ASID;
    smd_$unit_rec(TEST_UNIT)->hw = &test_hw;

    mock_validate_result = (int8_t)0xFF;   /* valid */
    mock_validate_calls = 0;
    mock_disp_type = 1;
    mock_disp_type_calls = 0;
    lock_calls = unlock_calls = advance_calls = wait_calls = 0;
    memset(&wait_ecs, 0, sizeof(wait_ecs));
    memset(&wait_vals, 0, sizeof(wait_vals));
    init_state_calls = 0;
    init_state_options = 0;
    mock_init_state_status = status_$ok;
    clear_kbd_calls = reset_globals_calls = clear_window_calls = 0;
    reset_globals_unit = -1;
    reset_globals_full = (boolean)0x7F;
    clear_window_rect = NULL;
    mock_clear_window_status = status_$ok;
    crash_calls = 0;
    crash_arg = NULL;
}

/* ---- tests ------------------------------------------------------------ */

/*
 * 0x00E6F5AA-0x00E6F5AC: a NON-negative smd_$validate_unit result is the
 * failure, and SMD_$INQ_DISP_TYPE is never reached.
 */
static void test_non_negative_validate_is_the_error_arm(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = 0;
    status_$t status = 0x5A5A5A5A;

    setup();
    mock_validate_result = 0;
    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(status_$display_invalid_unit_number, status);
    CHECK_EQ(1, mock_validate_calls);
    CHECK_EQ(0, mock_disp_type_calls);
    CHECK_EQ(0, lock_calls);
}

/* A negative (true) result goes on to query the display type. */
static void test_negative_validate_queries_display_type(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = 0;
    status_$t status = 0x5A5A5A5A;

    setup();
    mock_validate_result = (int8_t)0xFF;
    mock_disp_type = 0;                    /* the second error arm */
    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(1, mock_disp_type_calls);
    CHECK_EQ(status_$display_invalid_unit_number, status);
    CHECK_EQ(0, lock_calls);
}

/* A valid unit with a non-zero type reaches the lock and the borrow path. */
static void test_valid_unit_takes_the_lock(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = 0;
    status_$t status = 0x5A5A5A5A;

    setup();
    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(1, lock_calls);
    CHECK_EQ(1, unlock_calls);
    CHECK_EQ(status_$ok, status);
    CHECK_EQ(TEST_ASID, smd_$unit_rec(TEST_UNIT)->borrowed_asid);
    CHECK_EQ(TEST_UNIT, SMD_GLOBALS.asid_to_unit[TEST_ASID]);
    CHECK_EQ((int8_t)0xFF, (int8_t)test_hw.tracking_enabled);
    /* options >= 0 -> the keyboard cursor is cleared, no window clear */
    CHECK_EQ(1, clear_kbd_calls);
    CHECK_EQ(0, clear_window_calls);
    CHECK_EQ(1, reset_globals_calls);
    CHECK_EQ(TEST_UNIT, reset_globals_unit);
    CHECK_EQ(0, reset_globals_full);
}

/* 0x00E6F5EC: already borrowed -> unlock and the "already borrowed" status. */
static void test_already_borrowed(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = 0;
    status_$t status = 0;

    setup();
    smd_$unit_rec(TEST_UNIT)->borrowed_asid = 9;
    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(status_$display_already_borrowed_by_this_process, status);
    CHECK_EQ(1, lock_calls);
    CHECK_EQ(1, unlock_calls);
    CHECK_EQ(0, init_state_calls);
}

/*
 * 0x00E6F60E-0x00E6F644: the owned path advances SMD_$WIRED_DATA.ec_2 and then waits on
 * EC_$WAIT with the cursor eventcount in slot 0 and count+1 as its value.
 */
static void test_owned_display_waits_on_ec_wait(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = 0;
    status_$t status = 0;

    setup();
    smd_$unit_rec(TEST_UNIT)->owner_asid = 2;
    test_hw.cursor_ec.count = 0x1234;
    SMD_GLOBALS.response_pending[TEST_UNIT] = (int8_t)0xFF;  /* granted */

    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(1, advance_calls);
    CHECK_EQ(1, wait_calls);
    CHECK(wait_ecs.ec[0] == &test_hw.cursor_ec);
    CHECK(wait_ecs.ec[1] == NULL);
    CHECK(wait_ecs.ec[2] == NULL);
    CHECK_EQ(0x1235, wait_vals.val[0]);
    CHECK_EQ(0, wait_vals.val[1]);
    CHECK_EQ(0, wait_vals.val[2]);
    /* bset.b #7 on the byte at hw+0x4C is bit 15 of the word */
    CHECK_EQ(0x8000, test_hw.field_4c);
    CHECK_EQ(status_$ok, status);
}

/* A non-negative response byte denies the borrow (0x00E6F650 `bmi` grants). */
static void test_denied_when_response_byte_is_non_negative(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = 0;
    status_$t status = 0;

    setup();
    smd_$unit_rec(TEST_UNIT)->owner_asid = 2;
    SMD_GLOBALS.response_pending[TEST_UNIT] = 0;

    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(status_$display_borrow_request_denied_by_screen_manager, status);
    CHECK_EQ(1, unlock_calls);
    CHECK_EQ(0, init_state_calls);
    CHECK_EQ(0, smd_$unit_rec(TEST_UNIT)->borrowed_asid);
}

/*
 * 0x00E6F6C0-0x00E6F6E8: a negative options byte and a display type in the
 * 0xA86 bit set clears the window; a failing clear crashes with 0x0013000E.
 */
static void test_negative_options_clears_window_and_crashes(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = (int8_t)0xFF;
    status_$t status = 0;

    setup();
    *((uint16_t *)&test_hw) = 1;           /* type 1 is in 0xA86 */
    mock_clear_window_status = 0x00130099;

    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(0, clear_kbd_calls);          /* options < 0 skips it */
    CHECK_EQ(1, clear_window_calls);
    CHECK(clear_window_rect == (smd_rect_t *)((uint8_t *)&test_hw + 0x4E));
    CHECK_EQ(1, crash_calls);
    CHECK(crash_arg != NULL);
    CHECK_EQ(0x0013000E, *crash_arg);
    CHECK_EQ(status_$ok, status);          /* clr.l (A4) still runs */
}

/* A display type outside the 0xA86 bit set skips the window clear. */
static void test_type_outside_mask_skips_window_clear(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = (int8_t)0xFF;
    status_$t status = 0;

    setup();
    *((uint16_t *)&test_hw) = 0;           /* bit 0 of 0xA86 is clear */

    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(0, clear_window_calls);
    CHECK_EQ(0, crash_calls);
    CHECK_EQ(status_$ok, status);
}

/* A failing smd_$init_display_state leaves the status untouched. */
static void test_init_display_state_failure_returns_early(void)
{
    int16_t unit = TEST_UNIT;
    int8_t options = 0;
    status_$t status = 0;

    setup();
    mock_init_state_status = 0x00130004;

    SMD_$BORROW_DISPLAY(&unit, &options, &status);

    CHECK_EQ(0x00130004, status);
    CHECK_EQ(0, clear_kbd_calls);
    CHECK_EQ(0, reset_globals_calls);
}

int main(void)
{
    printf("SMD_$BORROW_DISPLAY tests:\n");
    RUN_TEST(non_negative_validate_is_the_error_arm);
    RUN_TEST(negative_validate_queries_display_type);
    RUN_TEST(valid_unit_takes_the_lock);
    RUN_TEST(already_borrowed);
    RUN_TEST(owned_display_waits_on_ec_wait);
    RUN_TEST(denied_when_response_byte_is_non_negative);
    RUN_TEST(negative_options_clears_window_and_crashes);
    RUN_TEST(type_outside_mask_skips_window_clear);
    RUN_TEST(init_display_state_failure_returns_early);
    printf("%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
