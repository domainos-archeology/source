/*
 * smd/test/test_get_idm_event.c - Unit tests for SMD_$GET_IDM_EVENT
 * (0x00E6EE28).
 *
 * The real smd/get_idm_event.c is #included below; only
 * SMD_$GET_UNIT_EVENT is mocked.
 *
 * Facts under test (bead source-t8yp):
 *   - the KEYSTROKE arm is a PLAIN word copy, not a byte swap:
 *
 *       00e6ee92    move.b (-0x4,A6),(0xa,A3)
 *       00e6ee98    move.b (-0x3,A6),(0xb,A3)
 *
 *     with the unit record at (-0x10,A6), -0x4/-0x3 are its 0x0C/0x0D bytes -
 *     the two halves of button_or_char - and they land at idm 0x0A/0x0B in
 *     the same order;
 *   - the dispatch is over the word table at 0x00E6EE6E,
 *     "00 16 00 16 00 24 00 30 00 0a", relative to 0x00E6EE6E:
 *       type 1 -> +0x16 = 0x00E6EE84   type 4 -> +0x30 = 0x00E6EE9E (epilogue)
 *       type 2 -> +0x16 = 0x00E6EE84   type 5 -> +0x0a = 0x00E6EE78
 *       type 3 -> +0x24 = 0x00E6EE92
 *     so type 4 and everything outside 1..5 write nothing at all;
 *   - types 1 and 2 latch button_or_char in SMD_GLOBALS.last_idm_button
 *     (0x1DA0) as well as delivering it;
 *   - type 5 is rewritten to type 1 and delivers the latched value.
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

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static uint16_t mock_type;
static smd_unit_event_t mock_event;
static status_$t mock_status;

void SMD_$GET_UNIT_EVENT(uint16_t *event_type, struct smd_unit_event_t *event_data,
                         status_$t *status_ret)
{
    *event_type = mock_type;
    memcpy(event_data, &mock_event, sizeof(mock_event));
    *status_ret = mock_status;
}

/* ------------------------------------------------------------------ */
/* Function under test                                                 */
/* ------------------------------------------------------------------ */

#include "../get_idm_event.c"

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void setup(uint16_t type, uint16_t button_or_char)
{
    mock_type = type;
    mock_status = 0;
    memset(&mock_event, 0, sizeof(mock_event));
    mock_event.pos = 0x00320064;
    mock_event.timestamp = 0x11223344;
    mock_event.field_08 = 0x5566;
    mock_event.unit = 1;
    mock_event.button_or_char = button_or_char;
    SMD_GLOBALS.last_idm_button = 0;
}

TEST(keystroke_is_a_plain_word_copy)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    /* 'A' in the high byte, modifier 0x0F in the low byte */
    setup(SMD_EVTYPE_KEYSTROKE, 0x410F);
    memset(&idm, 0xEE, sizeof(idm));

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    CHECK_EQ(SMD_EVTYPE_KEYSTROKE, type);
    /* The image's two byte moves are equivalent to this one word move, so
     * that is what is asserted.  smd_idm_event_t's char_code/modifier union
     * arm is a host-endian view of the same word, so it says nothing useful
     * on a little-endian host and is deliberately not checked here. */
    CHECK_EQ(0x410F, idm.data);        /* NOT 0x0F41 */
    /* keystrokes do not touch the latch */
    CHECK_EQ(0, SMD_GLOBALS.last_idm_button);
}

TEST(base_ten_bytes_are_copied)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    setup(SMD_EVTYPE_KEYSTROKE, 0x0102);
    memset(&idm, 0xEE, sizeof(idm));

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    CHECK_EQ(0x00320064, idm.timestamp);
    CHECK_EQ(0x11223344, idm.field_04);
    CHECK_EQ(0x5566, idm.field_08);
}

TEST(button_down_latches_and_delivers)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    setup(SMD_EVTYPE_BUTTON_DOWN, 0x0007);
    memset(&idm, 0xEE, sizeof(idm));

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    CHECK_EQ(SMD_EVTYPE_BUTTON_DOWN, type);
    CHECK_EQ(0x0007, idm.data);
    CHECK_EQ(0x0007, SMD_GLOBALS.last_idm_button);
}

TEST(button_up_latches_and_delivers)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    setup(SMD_EVTYPE_BUTTON_UP, 0x0004);
    memset(&idm, 0xEE, sizeof(idm));

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    CHECK_EQ(SMD_EVTYPE_BUTTON_UP, type);
    CHECK_EQ(0x0004, idm.data);
    CHECK_EQ(0x0004, SMD_GLOBALS.last_idm_button);
}

TEST(pointer_up_becomes_button_down_with_latch)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    setup(SMD_EVTYPE_POINTER_UP, 0xDEAD);
    SMD_GLOBALS.last_idm_button = 0x0002;
    memset(&idm, 0xEE, sizeof(idm));

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    CHECK_EQ(SMD_EVTYPE_BUTTON_DOWN, type);
    CHECK_EQ(0x0002, idm.data);
    /* the latch itself is not rewritten by this arm */
    CHECK_EQ(0x0002, SMD_GLOBALS.last_idm_button);
}

TEST(type_four_writes_no_data_word)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    setup(4, 0xBEEF);
    memset(&idm, 0, sizeof(idm));
    idm.data = 0x1234;

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    CHECK_EQ(4, type);
    CHECK_EQ(0x1234, idm.data);     /* table entry 0x30 -> the epilogue */
    CHECK_EQ(0, SMD_GLOBALS.last_idm_button);
}

TEST(type_zero_writes_no_data_word)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    setup(SMD_EVTYPE_NONE, 0xBEEF);
    memset(&idm, 0, sizeof(idm));
    idm.data = 0x4321;

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    /* "subq.w #0x1" makes 0 wrap to 0xFFFF, which fails the bcc bound */
    CHECK_EQ(SMD_EVTYPE_NONE, type);
    CHECK_EQ(0x4321, idm.data);
}

TEST(type_six_writes_no_data_word)
{
    uint16_t type;
    smd_idm_event_t idm;
    status_$t st = 0;

    setup(6, 0xBEEF);
    memset(&idm, 0, sizeof(idm));
    idm.data = 0x5678;

    SMD_$GET_IDM_EVENT(&type, &idm, &st);

    CHECK_EQ(6, type);
    CHECK_EQ(0x5678, idm.data);
}

int main(void)
{
    printf("test_get_idm_event:\n");

    RUN_TEST(keystroke_is_a_plain_word_copy);
    RUN_TEST(base_ten_bytes_are_copied);
    RUN_TEST(button_down_latches_and_delivers);
    RUN_TEST(button_up_latches_and_delivers);
    RUN_TEST(pointer_up_becomes_button_down_with_latch);
    RUN_TEST(type_four_writes_no_data_word);
    RUN_TEST(type_zero_writes_no_data_word);
    RUN_TEST(type_six_writes_no_data_word);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
