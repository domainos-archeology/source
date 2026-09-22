/*
 * tty/test/test_i_flush.c - Unit tests for TTY_$I_FLUSH_INPUT (0x00E1B7B0),
 * TTY_$I_FLUSH_OUTPUT (0x00E1B806) and TTY_$I_OUTPUT_BUFFER_DRAINED
 * (0x00E1B394).
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

void TTY_$I_ADVANCE_EC(m68k_ptr_t ec)
{ logf_call("advance(%lx);", (unsigned long)ec); }

static void mock_flow(uint32_t line_id, boolean a, boolean b)
{ logf_call("flow(%lx,%02x,%02x);", (unsigned long)line_id, (uint8_t)a, (uint8_t)b); }

#include "../i_flush.c"

static tty_desc_t tty;

static void reset(void)
{
    memset(&tty, 0, sizeof(tty));
    tty.line_id = 0x00030001;
    tty.input_ec = 0x1111; tty.output_ec = 0x2222;
    tty.input_read = 7; tty.input_head = 12; tty.input_tail = 20;
    tty.output_head = 3; tty.output_read = 9;
    tty.column = 33;
    log_reset();
}

TEST(drained_clears_bit0_and_wakes)
{
    reset();
    tty.state_flags = 0x0103;
    TTY_$I_OUTPUT_BUFFER_DRAINED(&tty);
    ASSERT_EQ(0x0102, tty.state_flags);
    ASSERT_STR("advance(2222);", call_log);
}

TEST(flush_input_resets_ring)
{
    reset();
    TTY_$I_FLUSH_INPUT(&tty);
    ASSERT_EQ(7, tty.input_tail);
    ASSERT_EQ(7, tty.input_head);
    ASSERT_EQ(7, tty.input_read);
    ASSERT_EQ(33, tty.saved_input_flags);
    ASSERT_STR("", call_log);
}

TEST(flush_input_wakes_waiting_writer)
{
    reset();
    tty.state_flags = TTY_STATUS_INPUT_WAIT | 0x40;
    TTY_$I_FLUSH_INPUT(&tty);
    ASSERT_EQ(0x40, tty.state_flags);
    ASSERT_STR("advance(2222);", call_log);
}

TEST(flush_input_flow_handler_args)
{
    reset();
    tty.flow_ctrl_handler = mock_flow;
    tty.input_flags = 0x2;
    TTY_$I_FLUSH_INPUT(&tty);
    ASSERT_STR("flow(30001,00,ff);", call_log);

    reset();
    tty.flow_ctrl_handler = mock_flow;
    tty.input_flags = 0xFFFD;
    TTY_$I_FLUSH_INPUT(&tty);
    ASSERT_STR("flow(30001,00,00);", call_log);
}

TEST(flush_output)
{
    reset();
    tty.state_flags = 0x1;
    TTY_$I_FLUSH_OUTPUT(&tty);
    ASSERT_EQ(3, tty.output_read);
    ASSERT_EQ(3, tty.output_head);
    ASSERT_EQ(0, tty.state_flags);
    ASSERT_STR("advance(2222);", call_log);
}

int main(void)
{
    printf("TTY_$I_FLUSH_* tests\n");
    RUN_TEST(drained_clears_bit0_and_wakes);
    RUN_TEST(flush_input_resets_ring);
    RUN_TEST(flush_input_wakes_waiting_writer);
    RUN_TEST(flush_input_flow_handler_args);
    RUN_TEST(flush_output);
    TEST_SUMMARY();
}
