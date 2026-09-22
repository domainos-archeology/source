/*
 * tty/test/test_i_err.c - Unit tests for TTY_$I_ERR (0x00E1BE08).
 * The status handler, TTY_$I_STORE_PARITY and TTY_$I_ADVANCE_EC are stubs;
 * the tests walk every arm of the decision tree at 0x00E1BE2C..0x00E1BE76.
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

static status_$t handler_result;
static uint32_t handler_line;
static boolean handler_flag;
static status_$t mock_status_handler(uint32_t line_id, boolean clear)
{
    handler_line = line_id;
    handler_flag = clear;
    logf_call("status;");
    return handler_result;
}

void TTY_$I_STORE_PARITY(tty_desc_t *tty, uint8_t ch)
{ (void)tty; logf_call("store(%02x);", ch); }
void TTY_$I_ADVANCE_EC(m68k_ptr_t ec)
{ logf_call("advance(%lx);", (unsigned long)ec); }

#include "../i_err.c"

static tty_desc_t tty;

static void run(uint8_t ch, status_$t err, uint32_t input_flags)
{
    memset(&tty, 0, sizeof(tty));
    tty.line_id = 0x00030002;
    tty.input_ec = 0x1111; tty.output_ec = 0x2222;
    tty.input_flags = input_flags;
    tty.status_handler = mock_status_handler;
    handler_result = err;
    log_reset();
    TTY_$I_ERR(&tty, ch);
}

#define FLAGGED "status;advance(1111);advance(2222);"

TEST(handler_arguments)
{
    run(0, 0, 0);
    ASSERT_EQ(0x00030002, handler_line);
    ASSERT_EQ(0xFF, (uint8_t)handler_flag);
}

TEST(framing_no_char_bit10)
{
    run(0, 0x360004, 0x400);
    ASSERT_STR("status;store(00);", call_log);
    ASSERT_EQ(0, tty.pending_signal);

    run(0, 0x360004, 0x800);            /* bit 10 clear -> flag */
    ASSERT_STR(FLAGGED, call_log);
    ASSERT_EQ(TTY_ERR_CALLBACK, tty.pending_signal);
}

TEST(dtr_drop_ignored_when_bit10)
{
    run(0, 0x36000b, 0x400);
    ASSERT_STR("status;store(00);", call_log);
    run(0x41, 0x36000b, 0x400);
    ASSERT_STR("status;store(41);", call_log);

    run(0, 0x36000b, 0x800);            /* bit 10 clear, bit 11 set, not 4/5 */
    ASSERT_STR(FLAGGED, call_log);
    run(0, 0x36000b, 0);
    ASSERT_STR(FLAGGED, call_log);
}

TEST(bit11_gates_parity_and_framing_with_char)
{
    run(0x41, 0x360004, 0x800);
    ASSERT_STR("status;store(41);", call_log);
    run(0x41, 0x360005, 0x800);
    ASSERT_STR("status;store(41);", call_log);
    run(0x41, 0x360005, 0x400);         /* bit 11 clear */
    ASSERT_STR(FLAGGED, call_log);
    run(0x41, 0x360006, 0x800);         /* other code */
    ASSERT_STR(FLAGGED, call_log);
    run(0, 0x360005, 0x800);            /* parity with no char takes the second tree */
    ASSERT_STR("status;store(00);", call_log);
}

TEST(flag_preserves_other_pending_bits)
{
    memset(&tty, 0, sizeof(tty));
    tty.status_handler = mock_status_handler;
    tty.pending_signal = 0x0502;
    handler_result = 0x360009;
    log_reset();
    TTY_$I_ERR(&tty, 0);
    ASSERT_EQ(0x0503, tty.pending_signal);
}

int main(void)
{
    printf("TTY_$I_ERR tests\n");
    RUN_TEST(handler_arguments);
    RUN_TEST(framing_no_char_bit10);
    RUN_TEST(dtr_drop_ignored_when_bit10);
    RUN_TEST(bit11_gates_parity_and_framing_with_char);
    RUN_TEST(flag_preserves_other_pending_bits);
    TEST_SUMMARY();
}
