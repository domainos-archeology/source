/*
 * smd/test/test_cond_input_u.c - Unit tests for SMD_$COND_INPUT_U
 * (0x00E6FA14).
 *
 * The real smd/cond_input_u.c is #included below; SMD_$GET_IDM_EVENT and
 * TERM_$CONTROL are mocked.
 *
 * Facts under test (bead source-dela):
 *   - the result is the BYTE in D0 ("move.b D2b,D0b" at 0x00E6FA9A), 0xFF or
 *     0, with nothing packed into a second byte;
 *   - TERM_$CONTROL's arguments, pushed right to left at
 *     0x00E6FA7A..0x00E6FA86, are
 *       arg1  pea (-0x215c,PC) -> 0x00E6D92C  SMD_ACQ_LOCK_DATA (word 0)
 *       arg2  pea (0x22,PC)    -> 0x00E6FAA6  the word 0x0024
 *       arg3  pea (-0x2154,PC) -> 0x00E6D92C  SMD_ACQ_LOCK_DATA (word 0)
 *       arg4  pea (-0x14,A6)   -> the same status cell GET_IDM_EVENT filled;
 *   - the control sequence is sent at most once per call: D4 starts 0 and is
 *     set to 0xFF at 0x00E6FA94, and 0x00E6FA70 "tst.b D4b / bmi.b" skips
 *     the arm once it is;
 *   - modifiers 0x00 and 0x0F deliver the character and stop; modifier 0x01
 *     sends the control sequence; every other modifier is ignored;
 *   - a non-zero status breaks out (0x00E6FA4C "tst.l (-0x14,A6)"), and the
 *     loop otherwise runs until the event type is 0 (0x00E6FA96 "tst.w D3w").
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"
#include "term/term.h"

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

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_globals_t SMD_GLOBALS;
uint16_t PROC1_$AS_ID;
uint16_t SMD_ACQ_LOCK_DATA = 0;   /* 0x00E6D92C, the word 0 */

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t type;
    uint8_t  char_code;
    uint8_t  modifier;
    status_$t status;
} scripted_event_t;

static scripted_event_t script[8];
static int script_len;
static int script_pos;
static int idm_calls;

void SMD_$GET_IDM_EVENT(uint16_t *event_type, smd_idm_event_t *idm_data,
                        status_$t *status_ret)
{
    idm_calls++;
    if (script_pos >= script_len) {
        /* run dry: an empty queue, which ends the loop */
        *event_type = SMD_EVTYPE_NONE;
        memset(idm_data, 0, sizeof(*idm_data));
        *status_ret = 0;
        return;
    }
    memset(idm_data, 0, sizeof(*idm_data));
    *event_type = script[script_pos].type;
    idm_data->char_code = script[script_pos].char_code;
    idm_data->modifier = script[script_pos].modifier;
    *status_ret = script[script_pos].status;
    script_pos++;
}

static int term_calls;
static uint16_t term_seen_line;
static uint16_t term_seen_option;
static uint16_t term_seen_value;
static status_$t *term_seen_status;

void TERM_$CONTROL(short *line_ptr, unsigned short *option_ptr,
                   unsigned short *value_ptr, status_$t *status_ret)
{
    term_calls++;
    term_seen_line = (uint16_t)*line_ptr;
    term_seen_option = *option_ptr;
    term_seen_value = *value_ptr;
    term_seen_status = status_ret;
}

/* ------------------------------------------------------------------ */
/* Function under test                                                 */
/* ------------------------------------------------------------------ */

#include "../cond_input_u.c"

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void reset(void)
{
    memset(script, 0, sizeof(script));
    script_len = 0;
    script_pos = 0;
    idm_calls = 0;
    term_calls = 0;
    term_seen_line = 0xFFFF;
    term_seen_option = 0xFFFF;
    term_seen_value = 0xFFFF;
    term_seen_status = NULL;
    PROC1_$AS_ID = 3;
    SMD_GLOBALS.asid_to_unit[3] = 1;
}

static void push(uint16_t type, uint8_t ch, uint8_t mod, status_$t st)
{
    script[script_len].type = type;
    script[script_len].char_code = ch;
    script[script_len].modifier = mod;
    script[script_len].status = st;
    script_len++;
}

TEST(return_type_is_one_byte)
{
    /* 0x00E6FA9A "move.b D2b,D0b" - a byte, not a word with a second flag. */
    CHECK_EQ(1, (long)sizeof(SMD_$COND_INPUT_U((uint8_t *)0)));
}

TEST(no_display_unit_returns_zero)
{
    uint8_t ch = 0x5A;

    reset();
    SMD_GLOBALS.asid_to_unit[3] = 0;

    CHECK_EQ(0, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ(0, idm_calls);
    CHECK_EQ(0x5A, ch);   /* untouched */
}

TEST(plain_keystroke_delivers_character)
{
    uint8_t ch = 0;

    reset();
    push(SMD_EVTYPE_KEYSTROKE, 'q', 0x00, 0);

    CHECK_EQ(0xFF, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ('q', ch);
    CHECK_EQ(1, idm_calls);   /* stops as soon as it has one */
    CHECK_EQ(0, term_calls);
}

TEST(modifier_0f_also_delivers)
{
    uint8_t ch = 0;

    reset();
    push(SMD_EVTYPE_KEYSTROKE, 'Z', 0x0F, 0);

    CHECK_EQ(0xFF, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ('Z', ch);
    CHECK_EQ(0, term_calls);
}

TEST(modifier_01_sends_the_control_cell)
{
    uint8_t ch = 0x11;

    reset();
    push(SMD_EVTYPE_KEYSTROKE, 'x', 0x01, 0);
    /* the queue then runs dry, ending the loop */

    CHECK_EQ(0, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ(0x11, ch);            /* nothing was delivered */
    CHECK_EQ(1, term_calls);
    CHECK_EQ(0x0024, term_seen_option);  /* 0x00E6FAA6 */
    CHECK_EQ(0, term_seen_line);         /* 0x00E6D92C */
    CHECK_EQ(0, term_seen_value);        /* 0x00E6D92C */
}

TEST(control_cell_is_the_named_static)
{
    /* The constant itself, in the code region just past the routine's rts. */
    CHECK_EQ(0x0024, smd_$cond_input_term_option);
    CHECK_EQ(2, (long)sizeof(smd_$cond_input_term_option));
}

TEST(control_sequence_is_sent_only_once)
{
    uint8_t ch = 0;

    reset();
    push(SMD_EVTYPE_KEYSTROKE, 'a', 0x01, 0);
    push(SMD_EVTYPE_KEYSTROKE, 'b', 0x01, 0);
    push(SMD_EVTYPE_KEYSTROKE, 'c', 0x01, 0);

    CHECK_EQ(0, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ(1, term_calls);       /* D4 latched at 0x00E6FA94 */
}

TEST(other_modifiers_are_skipped)
{
    uint8_t ch = 0;

    reset();
    push(SMD_EVTYPE_KEYSTROKE, 'a', 0x02, 0);
    push(SMD_EVTYPE_KEYSTROKE, 'b', 0x07, 0);
    push(SMD_EVTYPE_KEYSTROKE, 'c', 0x00, 0);

    CHECK_EQ(0xFF, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ('c', ch);
    CHECK_EQ(3, idm_calls);
    CHECK_EQ(0, term_calls);
}

TEST(non_keystroke_events_are_skipped)
{
    uint8_t ch = 0;

    reset();
    push(SMD_EVTYPE_BUTTON_DOWN, 0, 0, 0);
    push(SMD_EVTYPE_BUTTON_UP, 0, 0, 0);
    push(SMD_EVTYPE_KEYSTROKE, 'k', 0x00, 0);

    CHECK_EQ(0xFF, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ('k', ch);
    CHECK_EQ(3, idm_calls);
}

TEST(error_status_breaks_out)
{
    uint8_t ch = 0x22;

    reset();
    push(SMD_EVTYPE_KEYSTROKE, 'z', 0x00, 0x00130002);

    CHECK_EQ(0, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ(0x22, ch);
    CHECK_EQ(1, idm_calls);
}

TEST(empty_queue_returns_zero)
{
    uint8_t ch = 0x33;

    reset();   /* nothing scripted: the first event is type 0 */

    CHECK_EQ(0, SMD_$COND_INPUT_U(&ch));
    CHECK_EQ(0x33, ch);
    CHECK_EQ(1, idm_calls);
}

int main(void)
{
    printf("test_cond_input_u:\n");

    RUN_TEST(return_type_is_one_byte);
    RUN_TEST(no_display_unit_returns_zero);
    RUN_TEST(plain_keystroke_delivers_character);
    RUN_TEST(modifier_0f_also_delivers);
    RUN_TEST(modifier_01_sends_the_control_cell);
    RUN_TEST(control_cell_is_the_named_static);
    RUN_TEST(control_sequence_is_sent_only_once);
    RUN_TEST(other_modifiers_are_skipped);
    RUN_TEST(non_keystroke_events_are_skipped);
    RUN_TEST(error_status_breaks_out);
    RUN_TEST(empty_queue_returns_zero);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
