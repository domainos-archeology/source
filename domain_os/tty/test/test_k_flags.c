/* TTY_$K_SET_FLAG / INQ_FLAGS and the six input/output/echo flag routines (0x00E67422..0x00E6781C) */
#include "test_k_stubs.h"
void TTY_$I_SIGNAL(tty_desc_t *t, short s) { (void)t; logf_call("signal(%02x);", (unsigned)s); }
#include "../k_flags.c"

TEST(set_flag)
{
    short line = 0, flag = 0; char v = (char)0xFF; status_$t st = 0;
    k_reset(); tty.input_read = 1; tty.input_head = 1;
    TTY_$K_SET_FLAG(&line, &flag, &v, &st);
    ASSERT_EQ(TTY_STATUS_SIG_PEND, tty.state_flags); ASSERT_STR("desc(0);", call_log);
    k_reset(); tty.input_read = 1; tty.input_head = 4;
    TTY_$K_SET_FLAG(&line, &flag, &v, &st);
    ASSERT_STR("desc(0);signal(1a);", call_log);
    v = 0; tty.state_flags = 0xFFFF;
    TTY_$K_SET_FLAG(&line, &flag, &v, &st);
    ASSERT_EQ(0xFFEF, tty.state_flags);
    flag = 1; v = (char)0xFF; tty.state_flags = 0;
    TTY_$K_SET_FLAG(&line, &flag, &v, &st);
    ASSERT_EQ(0, tty.state_flags); ASSERT_EQ(0, st);
}
TEST(inq_flags)
{
    short line = 0; uint16_t f = 0xFFFF; status_$t st = 0;
    k_reset(); tty.state_flags = 0xFFEF;
    TTY_$K_INQ_FLAGS(&line, &f, &st); ASSERT_EQ(0, f);
    tty.state_flags = 0x10;
    TTY_$K_INQ_FLAGS(&line, &f, &st); ASSERT_EQ(1, f);
    f = 0x55; desc_status = 5; TTY_$K_INQ_FLAGS(&line, &f, &st); ASSERT_EQ(0x55, f);
}
TEST(bit_routines)
{
    short line = 0; uint16_t bit; char on = (char)0x80, off = 0x7f; status_$t st = 0; uint32_t out = 0;
    k_reset();
    bit = 31; TTY_$K_SET_INPUT_FLAG(&line, &bit, &on, &st); ASSERT_EQ(0x80000000, tty.input_flags);
    bit = 33; TTY_$K_SET_INPUT_FLAG(&line, &bit, &on, &st); ASSERT_EQ(0x80000002, tty.input_flags);
    bit = 31; TTY_$K_SET_INPUT_FLAG(&line, &bit, &off, &st); ASSERT_EQ(0x2, tty.input_flags);
    TTY_$K_INQ_INPUT_FLAGS(&line, &out, &st); ASSERT_EQ(0x2, out);
    bit = 4; TTY_$K_SET_OUTPUT_FLAG(&line, &bit, &on, &st); ASSERT_EQ(0x10, tty.output_flags);
    TTY_$K_INQ_OUTPUT_FLAGS(&line, &out, &st); ASSERT_EQ(0x10, out);
    TTY_$K_SET_OUTPUT_FLAG(&line, &bit, &off, &st); ASSERT_EQ(0, tty.output_flags);
    bit = 5; TTY_$K_SET_ECHO_FLAG(&line, &bit, &on, &st); ASSERT_EQ(0x20, tty.echo_flags);
    TTY_$K_INQ_ECHO_FLAGS(&line, &out, &st); ASSERT_EQ(0x20, out);
    TTY_$K_SET_ECHO_FLAG(&line, &bit, &off, &st); ASSERT_EQ(0, tty.echo_flags);
}
int main(void)
{
    RUN_TEST(set_flag); RUN_TEST(inq_flags); RUN_TEST(bit_routines);
    TEST_SUMMARY();
}
