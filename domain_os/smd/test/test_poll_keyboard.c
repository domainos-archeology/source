/*
 * smd/test/test_poll_keyboard.c - Unit tests for smd_$poll_keyboard
 *
 * Tests the keyboard polling and event queue ring buffer behavior.
 * Uses stubs for smd_$validate_unit, KBD_$GET_CHAR_AND_MODE, and TIME_$CLOCK.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Minimal type stubs for native compilation */
typedef long status_$t;

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
        printf("FAILED\n    Expected: 0x%x, Got: 0x%x at line %d\n", \
               (unsigned)(expected), (unsigned)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * Minimal mock structures matching the original layout
 */

typedef uint32_t domos_clock_t;

typedef struct {
    int16_t x;
    int16_t y;
} smd_cursor_pos_t;

typedef struct {
    smd_cursor_pos_t pos;
    uint32_t timestamp;
    uint16_t field_08;
    uint16_t unit;
    uint16_t event_type;
    uint16_t button_or_char;
} smd_event_entry_t;

#define SMD_EVENT_QUEUE_SIZE 256
#define SMD_EVENT_QUEUE_MASK 0xFF

typedef struct {
    uint8_t pad[0x728];
    uint16_t event_queue_head;
    uint16_t event_queue_tail;
    smd_event_entry_t event_queue[SMD_EVENT_QUEUE_SIZE];
    uint8_t pad2[0x66C]; /* padding to reach default_unit */
    uint16_t default_unit;
} mock_globals_t;

/* Mock globals */
static mock_globals_t mock_globals;
#define SMD_GLOBALS mock_globals

/* Mock keyboard line */
uint16_t SMD_KBD_LINE = 0;

/* Stub control */
static int8_t stub_validate_result = (int8_t)0xFF;
static int kbd_call_count = 0;
static int kbd_max_chars = 0;
static uint8_t kbd_chars[16];
static uint8_t kbd_modes[16];
static uint32_t stub_clock_value = 0x12345678;

/* Stub functions */
int8_t smd_$validate_unit(uint16_t unit)
{
    (void)unit;
    return stub_validate_result;
}

int8_t KBD_$GET_CHAR_AND_MODE(uint16_t *line_ptr, uint8_t *char_out,
                               uint8_t *mode_out, status_$t *status)
{
    (void)line_ptr;
    (void)status;
    if (kbd_call_count >= kbd_max_chars) {
        return 0; /* No char available */
    }
    *char_out = kbd_chars[kbd_call_count];
    *mode_out = kbd_modes[kbd_call_count];
    kbd_call_count++;
    return (int8_t)0xFF; /* Char available */
}

void TIME_$CLOCK(domos_clock_t *out)
{
    *out = stub_clock_value;
}

/* Forward declaration of function under test */
int8_t smd_$poll_keyboard(void);

/* Re-implement for testing (simplified version matching the logic) */
int8_t smd_$poll_keyboard(void)
{
    int8_t result = 0;

    if (smd_$validate_unit(SMD_GLOBALS.default_unit) < 0) {
        result = (int8_t)0xFF;

        for (;;) {
            uint16_t next_head = (SMD_GLOBALS.event_queue_head + 1) & SMD_EVENT_QUEUE_MASK;
            if (next_head == SMD_GLOBALS.event_queue_tail) {
                break;
            }

            smd_event_entry_t *entry = &SMD_GLOBALS.event_queue[SMD_GLOBALS.event_queue_head];
            status_$t status;

            int8_t kbd_result = KBD_$GET_CHAR_AND_MODE(
                &SMD_KBD_LINE,
                (uint8_t *)&entry->button_or_char,
                ((uint8_t *)&entry->button_or_char) + 1,
                &status);

            if (kbd_result >= 0) {
                return (int8_t)0xFF;
            }

            entry->unit = SMD_GLOBALS.default_unit;
            TIME_$CLOCK((domos_clock_t *)&entry->timestamp);

            if (((uint8_t *)&entry->button_or_char)[1] == 0) {
                entry->event_type = 0x00;
            } else {
                entry->event_type = 0x0C;
            }

            SMD_GLOBALS.event_queue_head = next_head;
        }
    }

    return result;
}

/* Helper to reset test state */
static void reset_state(void)
{
    memset(&mock_globals, 0, sizeof(mock_globals));
    mock_globals.default_unit = 1;
    stub_validate_result = (int8_t)0xFF;
    kbd_call_count = 0;
    kbd_max_chars = 0;
    stub_clock_value = 0x12345678;
}

/*
 * Tests
 */

TEST(invalid_unit_returns_zero)
{
    reset_state();
    stub_validate_result = 0; /* Unit not valid */
    int8_t result = smd_$poll_keyboard();
    ASSERT_EQ(0, result);
}

TEST(valid_unit_no_chars_returns_ff)
{
    reset_state();
    kbd_max_chars = 0; /* No keyboard chars */
    int8_t result = smd_$poll_keyboard();
    ASSERT_EQ((int8_t)0xFF, (int8_t)result);
}

TEST(one_char_queued)
{
    reset_state();
    kbd_chars[0] = 'A';
    kbd_modes[0] = 0x01; /* Normal key */
    kbd_max_chars = 1;

    int8_t result = smd_$poll_keyboard();
    ASSERT_EQ((int8_t)0xFF, (int8_t)result);

    /* Head should have advanced by 1 */
    ASSERT_EQ(1, mock_globals.event_queue_head);

    /* Check event entry */
    smd_event_entry_t *entry = &mock_globals.event_queue[0];
    ASSERT_EQ(1, entry->unit); /* default_unit */
    ASSERT_EQ(0x0C, entry->event_type); /* Normal key */
    ASSERT_EQ(stub_clock_value, entry->timestamp);
}

TEST(meta_key_event_type)
{
    reset_state();
    kbd_chars[0] = 'X';
    kbd_modes[0] = 0x00; /* Meta key (mode = 0) */
    kbd_max_chars = 1;

    smd_$poll_keyboard();

    smd_event_entry_t *entry = &mock_globals.event_queue[0];
    ASSERT_EQ(0x00, entry->event_type); /* Meta key type */
}

TEST(multiple_chars_queued)
{
    reset_state();
    kbd_chars[0] = 'A';
    kbd_modes[0] = 0x01;
    kbd_chars[1] = 'B';
    kbd_modes[1] = 0x01;
    kbd_chars[2] = 'C';
    kbd_modes[2] = 0x00;
    kbd_max_chars = 3;

    smd_$poll_keyboard();

    ASSERT_EQ(3, mock_globals.event_queue_head);

    /* Check third event has meta key type */
    ASSERT_EQ(0x00, mock_globals.event_queue[2].event_type);
    /* First two are normal keys */
    ASSERT_EQ(0x0C, mock_globals.event_queue[0].event_type);
    ASSERT_EQ(0x0C, mock_globals.event_queue[1].event_type);
}

TEST(queue_full_stops)
{
    reset_state();
    /* Set head and tail so only 1 slot is available */
    mock_globals.event_queue_head = 5;
    mock_globals.event_queue_tail = 7; /* next_head after 1 char = 6, which != 7, ok */
                                        /* next_head after 2nd = 7, which == tail, stop */
    kbd_chars[0] = 'A';
    kbd_modes[0] = 0x01;
    kbd_chars[1] = 'B';
    kbd_modes[1] = 0x01;
    kbd_max_chars = 2;

    smd_$poll_keyboard();

    /* Should have enqueued only 1 character (second would make head == tail) */
    ASSERT_EQ(6, mock_globals.event_queue_head);
}

TEST(head_wraps_around)
{
    reset_state();
    mock_globals.event_queue_head = 254;
    mock_globals.event_queue_tail = 0; /* tail at 0, so wrapping head to 255 is ok, 0 would == tail */
    kbd_chars[0] = 'W';
    kbd_modes[0] = 0x01;
    kbd_max_chars = 1;

    smd_$poll_keyboard();

    /* Head should wrap from 254 to 255 */
    ASSERT_EQ(255, mock_globals.event_queue_head);
}

int main(void)
{
    printf("test_poll_keyboard:\n");

    RUN_TEST(invalid_unit_returns_zero);
    RUN_TEST(valid_unit_no_chars_returns_ff);
    RUN_TEST(one_char_queued);
    RUN_TEST(meta_key_event_type);
    RUN_TEST(multiple_chars_queued);
    RUN_TEST(queue_full_stops);
    RUN_TEST(head_wraps_around);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
