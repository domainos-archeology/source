/*
 * smd/test/test_inq_kbd_type.c - Unit tests for SMD_$INQ_KBD_TYPE
 * (0x00E6E122).
 *
 * The real smd/inq_kbd_type.c is #included below and the real function is
 * called; only KBD_$INQ_KBD_TYPE is mocked.
 *
 * Facts under test (bead source-81a6):
 *   - the copy loop is buffer[i] = local_buf[i], NOT local_buf[i+1]:
 *
 *       00e6e162    moveq #0x1,D1
 *       00e6e164    move.b (-0x79,A6,D1w*0x1),(-0x1,A2,D1w*0x1)
 *       00e6e16a    addq.w #0x1,D1w
 *       00e6e16c    dbf D0w,0x00e6e164
 *
 *     with local_buf at (-0x78,A6) and the caller's buffer at (A2), the two
 *     biased displacements cancel the 1-based index;
 *   - the copy length is min(*buf_size, *length) and the dbf runs
 *     copy_len times (00e6e15e "subq.w #0x1" / 00e6e160 "bmi.b");
 *   - the "line" argument handed to KBD_$INQ_KBD_TYPE is the shared cell at
 *     0x00E6D92C, SMD_ACQ_LOCK_DATA, whose value is the word 0
 *     ("pea (-0x818,PC)" at 0x00E6E142);
 *   - a short buffer still copies, then reports 0x0013000C
 *     (00e6e176 "move.l #0x13000c,(A4)");
 *   - length 2 with buffer[1] == '@' becomes length 1 and a space, otherwise
 *     buffer[1] is folded to (buffer[1] & 0x1F) + 0x60.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"
#include "kbd/kbd.h"

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

uint16_t SMD_ACQ_LOCK_DATA = 0;   /* 0x00E6D92C, the word 0 */

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static const char *mock_kbd_string;
static uint16_t mock_kbd_length;
static status_$t mock_kbd_status;
static int kbd_calls;
static uint16_t kbd_seen_line;

void KBD_$INQ_KBD_TYPE(uint16_t *line_ptr, uint8_t *type_buf,
                       uint16_t *length_ret, status_$t *status_ret)
{
    kbd_calls++;
    kbd_seen_line = *line_ptr;

    /* Poison the whole local buffer, then lay the answer down at [0]. */
    memset(type_buf, 0xEE, 120);
    memcpy(type_buf, mock_kbd_string, mock_kbd_length);

    *length_ret = mock_kbd_length;
    *status_ret = mock_kbd_status;
}

/* ------------------------------------------------------------------ */
/* Function under test                                                 */
/* ------------------------------------------------------------------ */

#include "../inq_kbd_type.c"

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void setup(const char *s, uint16_t len, status_$t st)
{
    mock_kbd_string = s;
    mock_kbd_length = len;
    mock_kbd_status = st;
    kbd_calls = 0;
    kbd_seen_line = 0xFFFF;
}

TEST(copy_is_not_offset_by_one)
{
    uint8_t buf[16];
    uint16_t size = sizeof(buf);
    uint16_t len = 0;
    status_$t st = 0x7F7F7F7F;

    setup("apollo", 6, 0);
    memset(buf, 0, sizeof(buf));

    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);

    CHECK_EQ(1, kbd_calls);
    CHECK_EQ(0, st);
    CHECK_EQ(6, len);
    CHECK_EQ(0, memcmp(buf, "apollo", 6));
    /* the first byte must be 'a', not 'p' */
    CHECK_EQ('a', buf[0]);
    CHECK_EQ('o', buf[5]);
    /* nothing past the copy length is touched */
    CHECK_EQ(0, buf[6]);
}

TEST(line_argument_is_the_shared_zero_cell)
{
    uint8_t buf[16];
    uint16_t size = sizeof(buf);
    uint16_t len = 0;
    status_$t st = 0;

    setup("xyz", 3, 0);
    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);
    CHECK_EQ(0, kbd_seen_line);
}

TEST(zero_length_copies_nothing)
{
    uint8_t buf[4] = { 0x11, 0x22, 0x33, 0x44 };
    uint16_t size = sizeof(buf);
    uint16_t len = 0;
    status_$t st = 0;

    setup("", 0, 0);
    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);

    CHECK_EQ(0x11, buf[0]);
    CHECK_EQ(0x22, buf[1]);
    CHECK_EQ(0, len);
    CHECK_EQ(0, st);
}

TEST(short_buffer_copies_then_reports)
{
    uint8_t buf[8];
    uint16_t size = 3;
    uint16_t len = 0;
    status_$t st = 0;

    setup("abcdef", 6, 0);
    memset(buf, 0, sizeof(buf));

    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);

    /* exactly *buf_size bytes, from the front of the local buffer */
    CHECK_EQ(0, memcmp(buf, "abc", 3));
    CHECK_EQ(0, buf[3]);
    CHECK_EQ(6, len);
    CHECK_EQ(0x0013000C, st);
}

TEST(kbd_error_returns_immediately)
{
    uint8_t buf[8] = { 0 };
    uint16_t size = sizeof(buf);
    uint16_t len = 0;
    status_$t st = 0;

    setup("abc", 3, 0x00130004);
    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);

    CHECK_EQ(0x00130004, st);
    CHECK_EQ(0, buf[0]);   /* 00e6e152 "bne.b" skips the whole tail */
}

TEST(two_char_at_sign_becomes_length_one_and_space)
{
    uint8_t buf[8];
    uint16_t size = sizeof(buf);
    uint16_t len = 0;
    status_$t st = 0;

    setup("A@", 2, 0);
    memset(buf, 0, sizeof(buf));

    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);

    CHECK_EQ('A', buf[0]);
    CHECK_EQ(0x20, buf[1]);
    CHECK_EQ(1, len);
}

TEST(two_char_other_is_folded_to_lowercase)
{
    uint8_t buf[8];
    uint16_t size = sizeof(buf);
    uint16_t len = 0;
    status_$t st = 0;

    setup("A3", 2, 0);
    memset(buf, 0, sizeof(buf));

    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);

    CHECK_EQ('A', buf[0]);
    /* 00e6e196 "moveq #0x1f,D0" / and.b / addi.b #0x60 */
    CHECK_EQ((uint8_t)(('3' & 0x1F) + 0x60), buf[1]);
    CHECK_EQ(2, len);
}

TEST(length_three_is_left_alone)
{
    uint8_t buf[8];
    uint16_t size = sizeof(buf);
    uint16_t len = 0;
    status_$t st = 0;

    setup("A@Z", 3, 0);
    memset(buf, 0, sizeof(buf));

    SMD_$INQ_KBD_TYPE(&size, buf, &len, &st);

    CHECK_EQ(0, memcmp(buf, "A@Z", 3));
    CHECK_EQ(3, len);
}

int main(void)
{
    printf("test_inq_kbd_type:\n");

    RUN_TEST(copy_is_not_offset_by_one);
    RUN_TEST(line_argument_is_the_shared_zero_cell);
    RUN_TEST(zero_length_copies_nothing);
    RUN_TEST(short_buffer_copies_then_reports);
    RUN_TEST(kbd_error_returns_immediately);
    RUN_TEST(two_char_at_sign_becomes_length_one_and_space);
    RUN_TEST(two_char_other_is_folded_to_lowercase);
    RUN_TEST(length_three_is_left_alone);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
