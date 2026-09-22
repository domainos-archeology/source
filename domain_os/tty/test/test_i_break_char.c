/*
 * tty/test/test_i_break_char.c - Unit tests for TTY_$I_BREAK_CHAR
 * (0x00E1B8B0).  The real tty_$i_buf_insert is included; XMIT, ADVANCE_EC
 * and SIGNAL are recording stubs.
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

void TTY_$I_XMIT_CHAR(tty_desc_t *tty, uint8_t ch)
{ (void)tty; logf_call("xmit(%02x);", ch); }
void TTY_$I_ADVANCE_EC(m68k_ptr_t ec)
{ logf_call("advance(%lx);", (unsigned long)ec); }
void TTY_$I_SIGNAL(tty_desc_t *tty, short sig)
{ (void)tty; logf_call("signal(%02x);", (unsigned)sig); }

#include "../i_buf_insert.c"
#include "../i_break_char.c"

static tty_desc_t tty;

static void reset(void)
{
    memset(&tty, 0, sizeof(tty));
    tty.input_read = 1; tty.input_head = 1; tty.input_tail = 1; tty.input_size = 0x100;
    tty.input_ec = 0x1111; tty.output_ec = 0x2222;
    tty.column = 7;
    log_reset();
}

TEST(stores_commits_and_wakes_reader)
{
    reset();
    tty.input_tail = 4;
    TTY_$I_BREAK_CHAR(&tty, '\n');
    ASSERT_EQ('\n', tty.input_buffer[3]);
    ASSERT_EQ(5, tty.input_tail);
    ASSERT_EQ(5, tty.input_head);
    ASSERT_EQ(7, tty.saved_input_flags);
    ASSERT_STR("advance(1111);", call_log);
}

TEST(echo_when_input_bit0)
{
    reset();
    tty.input_flags = 0x1;
    TTY_$I_BREAK_CHAR(&tty, 0x0d);
    ASSERT_STR("xmit(0d);advance(1111);", call_log);
}

TEST(sig_pend_sends_1a)
{
    reset();
    tty.state_flags = TTY_STATUS_SIG_PEND;
    TTY_$I_BREAK_CHAR(&tty, 0x0a);
    ASSERT_STR("advance(1111);signal(1a);", call_log);
    ASSERT_EQ(TTY_STATUS_SIG_PEND, tty.state_flags);   /* not cleared */
}

TEST(input_wait_cleared_and_output_ec_advanced)
{
    reset();
    tty.state_flags = TTY_STATUS_INPUT_WAIT | 0x0100;
    TTY_$I_BREAK_CHAR(&tty, 0x0a);
    ASSERT_STR("advance(1111);advance(2222);", call_log);
    ASSERT_EQ(0x0100, tty.state_flags);
}

TEST(full_buffer_still_commits)
{
    reset();
    tty.input_read = 3; tty.input_head = 3; tty.input_tail = 2;   /* full */
    TTY_$I_BREAK_CHAR(&tty, 0x0a);
    ASSERT_EQ(0, tty.input_buffer[1]);
    ASSERT_EQ(2, tty.input_tail);
    ASSERT_EQ(2, tty.input_head);
}

int main(void)
{
    printf("TTY_$I_BREAK_CHAR tests\n");
    RUN_TEST(stores_commits_and_wakes_reader);
    RUN_TEST(echo_when_input_bit0);
    RUN_TEST(sig_pend_sends_1a);
    RUN_TEST(input_wait_cleared_and_output_ec_advanced);
    RUN_TEST(full_buffer_still_commits);
    TEST_SUMMARY();
}
