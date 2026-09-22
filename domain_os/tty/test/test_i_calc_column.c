/*
 * tty/test/test_i_calc_column.c - Unit tests for TTY_$I_CALC_COLUMN
 * (0x00E1B4B6).  Pure function over a buffer record: the tests pin the
 * data[pos-1] addressing, the 1..0x100 wrap, the tab rounding, the
 * backspace floor at 0, CR, the echo_flags bit-4 gate for control
 * characters / DEL, and the empty walk.
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

#include "../i_calc_column.c"

/* {read(head), tail, size, data[256]} as tty_desc_t lays it out */
static struct { uint16_t head, tail, size; uint8_t data[256]; } __attribute__((packed)) buf;

static void fill(int16_t start, const char *s)
{
    int16_t pos = start;
    memset(&buf, 0, sizeof(buf));
    buf.size = 0x100;
    while (*s) {
        buf.data[pos - 1] = (uint8_t)*s++;
        pos = (pos == 0x100) ? 1 : (int16_t)(pos + 1);
    }
    buf.tail = (uint16_t)pos;
}

TEST(empty_range_returns_column)
{
    fill(5, "");
    ASSERT_EQ(17, TTY_$I_CALC_COLUMN(&buf, 5, 17, 0));
}

TEST(printables_add_one_each)
{
    fill(1, "abc");
    ASSERT_EQ(3, TTY_$I_CALC_COLUMN(&buf, 1, 0, 0));
    ASSERT_EQ(13, TTY_$I_CALC_COLUMN(&buf, 1, 10, 0));
}

TEST(tab_rounds_up_to_eight)
{
    fill(1, "\t");
    /* ((col + 7) >> 3) << 3 rounds UP to a multiple of 8, so a tab at a
     * column that is already a multiple of 8 adds nothing (0x00E1B4DE). */
    ASSERT_EQ(0, TTY_$I_CALC_COLUMN(&buf, 1, 0, 0));
    ASSERT_EQ(8, TTY_$I_CALC_COLUMN(&buf, 1, 1, 0));
    ASSERT_EQ(8, TTY_$I_CALC_COLUMN(&buf, 1, 7, 0));
    ASSERT_EQ(8, TTY_$I_CALC_COLUMN(&buf, 1, 8, 0));
    ASSERT_EQ(16, TTY_$I_CALC_COLUMN(&buf, 1, 9, 0));
    ASSERT_EQ(0, TTY_$I_CALC_COLUMN(&buf, 1, 0xFFFF, 0));  /* word truncation */
}

TEST(backspace_floors_at_zero)
{
    fill(1, "\b");
    ASSERT_EQ(0, TTY_$I_CALC_COLUMN(&buf, 1, 0, 0));
    ASSERT_EQ(4, TTY_$I_CALC_COLUMN(&buf, 1, 5, 0));
}

TEST(cr_resets)
{
    fill(1, "abc\rx");
    ASSERT_EQ(1, TTY_$I_CALC_COLUMN(&buf, 1, 40, 0));
}

TEST(controls_gated_by_echo_bit4)
{
    fill(1, "\x01\x7f\x1f");
    ASSERT_EQ(0, TTY_$I_CALC_COLUMN(&buf, 1, 0, 0));
    ASSERT_EQ(6, TTY_$I_CALC_COLUMN(&buf, 1, 0, 0x10));
    ASSERT_EQ(0, TTY_$I_CALC_COLUMN(&buf, 1, 0, 0xFFFFFFEF));
}

TEST(high_bytes_are_printable)
{
    fill(1, "\x80\xff");
    ASSERT_EQ(2, TTY_$I_CALC_COLUMN(&buf, 1, 0, 0));
}

TEST(wraps_from_256_to_1)
{
    fill(0xFF, "xyz");           /* positions 0xFF, 0x100, 1 */
    ASSERT_EQ(2, buf.tail);
    ASSERT_EQ(3, TTY_$I_CALC_COLUMN(&buf, 0xFF, 0, 0));
    ASSERT_EQ(2, TTY_$I_CALC_COLUMN(&buf, 0x100, 0, 0));
}

int main(void)
{
    printf("TTY_$I_CALC_COLUMN tests\n");
    RUN_TEST(empty_range_returns_column);
    RUN_TEST(printables_add_one_each);
    RUN_TEST(tab_rounds_up_to_eight);
    RUN_TEST(backspace_floors_at_zero);
    RUN_TEST(cr_resets);
    RUN_TEST(controls_gated_by_echo_bit4);
    RUN_TEST(high_bytes_are_printable);
    RUN_TEST(wraps_from_256_to_1);
    TEST_SUMMARY();
}
