/*
 * tty/test/test_i_kill_line.c - Unit tests for TTY_$I_KILL_LINE (0x00E1B6AC).
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

static tty_desc_t tty;

void TTY_$I_DELETE_CHAR(tty_desc_t *t)
{
    (void)t;
    logf_call("del;");
    /* emulate one character removed */
    tty.input_tail = (tty.input_tail == 1) ? 0x100 : (uint16_t)(tty.input_tail - 1);
}
void TTY_$I_ECHO_CHAR(tty_desc_t *t, uint8_t ch)
{ (void)t; logf_call("echo(%02x);", ch); }
void TTY_$I_NEWLINE(tty_desc_t *t)
{ (void)t; logf_call("nl;"); }

#include "../i_kill_line.c"

static void reset(void)
{
    memset(&tty, 0, sizeof(tty));
    tty.func_chars[2] = 0x15;
    log_reset();
}

TEST(crt_mode_deletes_until_head)
{
    reset();
    tty.echo_flags = 0x4;
    tty.input_read = 1; tty.input_head = 3; tty.input_tail = 6;
    TTY_$I_KILL_LINE(&tty);
    ASSERT_STR("del;del;del;", call_log);
    ASSERT_EQ(3, tty.input_tail);

    reset();
    tty.echo_flags = 0x4;
    tty.input_head = 6; tty.input_tail = 6;
    TTY_$I_KILL_LINE(&tty);
    ASSERT_STR("", call_log);
}

TEST(plain_echo_and_discard)
{
    reset();
    tty.input_read = 4; tty.input_head = 4; tty.input_tail = 9;
    TTY_$I_KILL_LINE(&tty);
    ASSERT_STR("echo(15);", call_log);
    ASSERT_EQ(4, tty.input_tail);
    ASSERT_EQ(4, tty.input_head);
}

TEST(plain_with_newline_bit5)
{
    reset();
    tty.echo_flags = 0x20;
    tty.input_read = 4; tty.input_head = 4; tty.input_tail = 9;
    TTY_$I_KILL_LINE(&tty);
    ASSERT_STR("echo(15);nl;", call_log);
}

TEST(tail_at_read_commits_head)
{
    reset();
    tty.input_read = 4; tty.input_head = 7; tty.input_tail = 4;
    TTY_$I_KILL_LINE(&tty);
    ASSERT_EQ(4, tty.input_head);
    ASSERT_EQ(4, tty.input_tail);

    reset();
    tty.input_read = 4; tty.input_head = 4; tty.input_tail = 4;
    TTY_$I_KILL_LINE(&tty);
    ASSERT_EQ(4, tty.input_head);
}

int main(void)
{
    printf("TTY_$I_KILL_LINE tests\n");
    RUN_TEST(crt_mode_deletes_until_head);
    RUN_TEST(plain_echo_and_discard);
    RUN_TEST(plain_with_newline_bit5);
    RUN_TEST(tail_at_read_commits_head);
    TEST_SUMMARY();
}
