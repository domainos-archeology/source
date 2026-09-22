/*
 * tty/test/test_i_delete_char.c - Unit tests for TTY_$I_DELETE_CHAR
 * (0x00E1B538).  The real TTY_$I_CALC_COLUMN is included; XMIT and
 * ADVANCE_EC are recording stubs.
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

void TTY_$I_XMIT_CHAR(tty_desc_t *tty, uint8_t ch)
{ (void)tty; logf_call("%02x ", ch); }
void TTY_$I_ADVANCE_EC(m68k_ptr_t ec)
{ logf_call("advance(%lx) ", (unsigned long)ec); }

#include "../i_calc_column.c"
#include "../i_delete_char.c"

static tty_desc_t tty;

/* line "text" committed at read=head=1, tail after it */
static void reset(const char *text)
{
    size_t n = strlen(text);
    memset(&tty, 0, sizeof(tty));
    tty.input_read = 1; tty.input_head = 1; tty.input_size = 0x100;
    memcpy(tty.input_buffer, text, n);
    tty.input_tail = (uint16_t)(1 + n);
    tty.output_ec = 0x2222;
    tty.input_flags = 0x1;              /* echo on */
    tty.func_chars[0] = 0x7f;
    log_reset();
}

TEST(empty_returns)
{
    reset("");
    tty.input_head = 1;                 /* tail == head */
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_EQ(1, tty.input_tail);
    ASSERT_STR("", call_log);
}

TEST(tail_at_read_commits)
{
    reset("");
    tty.input_head = 9; tty.input_read = 1; tty.input_tail = 1;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_EQ(1, tty.input_head);
    ASSERT_EQ(1, tty.input_tail);
    ASSERT_STR("", call_log);
}

TEST(wrap_1_to_256)
{
    reset("");
    tty.input_head = 0x80; tty.input_read = 0x80; tty.input_tail = 1;
    tty.input_buffer[0xFF] = 'q';
    tty.echo_flags = 0;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_EQ(0x100, tty.input_tail);
    ASSERT_STR("7f ", call_log);        /* func_chars[0] */
}

TEST(emptying_wakes_writer)
{
    reset("a");
    tty.state_flags = TTY_STATUS_INPUT_WAIT;
    tty.input_flags = 0;                /* no echo */
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_EQ(1, tty.input_tail);
    ASSERT_EQ(0, tty.state_flags);
    ASSERT_STR("advance(2222) ", call_log);
}

TEST(no_echo_no_output)
{
    reset("ab");
    tty.input_flags = 0;
    tty.echo_flags = 0x1;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_EQ(2, tty.input_tail);
    ASSERT_STR("", call_log);
}

TEST(crt_erase_printable)
{
    reset("ab");
    tty.echo_flags = 0x1;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("08 ", call_log);

    reset("ab");
    tty.echo_flags = 0x3;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("08 20 08 ", call_log);
}

TEST(crt_erase_control_two_columns_or_nothing)
{
    reset("a\x03");
    tty.echo_flags = 0x11;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("08 08 ", call_log);

    reset("a\x7f");
    tty.echo_flags = 0x1;               /* bit 4 clear -> return before erase */
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_EQ(2, tty.input_tail);
    ASSERT_STR("", call_log);
}

TEST(crt_erase_tab_uses_calc_column)
{
    reset("abc\t");                     /* col after "abc" = 3 -> tab to 8: 5 BS */
    tty.echo_flags = 0x1;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("08 08 08 08 08 ", call_log);

    reset("\t");                        /* col 0 -> 8 columns */
    tty.echo_flags = 0x1;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("08 08 08 08 08 08 08 08 ", call_log);

    reset("abcdefgh\t");               /* col 8 -> 8 - 0 = 8 */
    tty.echo_flags = 0x1;
    tty.saved_input_flags = 0;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("08 08 08 08 08 08 08 08 ", call_log);
}

TEST(echo_erase_backslash_once)
{
    reset("ab");
    tty.echo_flags = 0x8;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("5c 62 ", call_log);
    ASSERT_EQ(0x80, tty.state_flags & 0x80);

    log_reset();
    TTY_$I_DELETE_CHAR(&tty);           /* bit 7 set -> no second backslash */
    ASSERT_STR("61 ", call_log);
}

TEST(echo_erase_controls)
{
    reset("a\x01");
    tty.echo_flags = 0x18;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("5c 5e 41 ", call_log);

    reset("a\x7f");
    tty.echo_flags = 0x18;
    tty.state_flags = 0x80;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("5e 3f ", call_log);
}

TEST(crt_takes_priority_over_echo_erase)
{
    reset("ab");
    tty.echo_flags = 0x9;
    TTY_$I_DELETE_CHAR(&tty);
    ASSERT_STR("08 ", call_log);
}

int main(void)
{
    printf("TTY_$I_DELETE_CHAR tests\n");
    RUN_TEST(empty_returns);
    RUN_TEST(tail_at_read_commits);
    RUN_TEST(wrap_1_to_256);
    RUN_TEST(emptying_wakes_writer);
    RUN_TEST(no_echo_no_output);
    RUN_TEST(crt_erase_printable);
    RUN_TEST(crt_erase_control_two_columns_or_nothing);
    RUN_TEST(crt_erase_tab_uses_calc_column);
    RUN_TEST(echo_erase_backslash_once);
    RUN_TEST(echo_erase_controls);
    RUN_TEST(crt_takes_priority_over_echo_erase);
    TEST_SUMMARY();
}
