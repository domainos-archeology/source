/* tty/test/test_i_store_parity.c - TTY_$I_STORE_PARITY (0x00E1BCFC), with
 * the real tty_$i_buf_insert. */
#include "tty/tty_internal.h"
#include "time/time.h"
#include "test_harness.h"

void TTY_$I_ADVANCE_EC(m68k_ptr_t ec) { logf_call("advance(%lx);", (unsigned long)ec); }
void TTY_$I_SIGNAL(tty_desc_t *t, short sig) { (void)t; logf_call("signal(%02x);", (unsigned)sig); }
void TIME_$CLOCK(clock_t *c) { c->high = 0xAABBCCDD; c->low = 0xEEFF; logf_call("clock;"); }

#include "../i_buf_insert.c"
#include "../i_store_parity.c"

static tty_desc_t tty;

static void reset(uint16_t tail)
{
    memset(&tty, 0, sizeof(tty));
    tty.input_read = 1; tty.input_head = 1; tty.input_tail = tail; tty.input_size = 0x100;
    tty.input_ec = 0x1111;
    log_reset();
}

TEST(marker_without_bit12)
{
    reset(1);
    TTY_$I_STORE_PARITY(&tty, 0x41);
    ASSERT_EQ(0x00, tty.input_buffer[0]);
    ASSERT_EQ(2, tty.input_tail);
    ASSERT_STR("", call_log);
}

TEST(escape_sequence_with_bit12)
{
    reset(1);
    tty.input_flags = 0x1000;
    TTY_$I_STORE_PARITY(&tty, 0x41);
    ASSERT_EQ(0xFF, tty.input_buffer[0]);
    ASSERT_EQ(0x00, tty.input_buffer[1]);
    ASSERT_EQ(0x41, tty.input_buffer[2]);
    ASSERT_EQ(4, tty.input_tail);
}

TEST(break_mode_enough_chars_completes_line)
{
    reset(3);
    tty.break_mode = 1; tty.min_chars = 2;
    TTY_$I_STORE_PARITY(&tty, 0);          /* tail -> 4, count = 3 >= 2 */
    ASSERT_EQ(5, tty.input_head);          /* tail + 1 */
    ASSERT_STR("advance(1111);", call_log);

    reset(0xFF);
    tty.input_read = 0x80;
    tty.break_mode = 1; tty.min_chars = 0;
    tty.state_flags = TTY_STATUS_SIG_PEND;
    TTY_$I_STORE_PARITY(&tty, 0);          /* tail -> 0x100, head wraps to 1 */
    ASSERT_EQ(0x100, tty.input_tail);
    ASSERT_EQ(1, tty.input_head);
    ASSERT_STR("advance(1111);signal(1a);", call_log);
}

TEST(break_mode_too_few_chars)
{
    reset(1);
    tty.break_mode = 1; tty.min_chars = 5;
    TTY_$I_STORE_PARITY(&tty, 0);
    ASSERT_EQ(1, tty.input_head);
    ASSERT_STR("", call_log);

    reset(1);
    tty.break_mode = 3; tty.min_chars = 5;
    TTY_$I_STORE_PARITY(&tty, 0);
    ASSERT_STR("clock;", call_log);
    ASSERT_EQ(0xAABBCCDD, tty.last_input_clock_high);
    ASSERT_EQ(0xEEFF, tty.last_input_clock_low);
}

int main(void)
{
    printf("TTY_$I_STORE_PARITY tests\n");
    RUN_TEST(marker_without_bit12);
    RUN_TEST(escape_sequence_with_bit12);
    RUN_TEST(break_mode_enough_chars_completes_line);
    RUN_TEST(break_mode_too_few_chars);
    TEST_SUMMARY();
}
