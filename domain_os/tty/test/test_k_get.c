/* TTY_$K_GET (0x00E1C3D2): canonical and non-canonical copies, the peek
 * option, the flow-control re-enable, the class 0x0C/0x03/0x0E/0x0B stops,
 * the buffer-full and wait paths, and the pending-error epilogue. */
#include "test_k_stubs.h"
void TTY_$I_SIGNAL(tty_desc_t *t, short s) { (void)t; logf_call("signal(%02x);", (unsigned)s); }
static int wait_sets_flag; static status_$t wait_status;
void tty_$i_wait(tty_desc_t *t, char wf, char *flag, uint16_t count, status_$t *st)
{
    (void)t; logf_call("wait(%02x,%u);", (uint8_t)wf, count);
    if (wait_sets_flag) *flag = (char)0xFF;
    *st = wait_status;
}
static void mock_flow(uint32_t id, boolean a, boolean b) { logf_call("flow(%lx,%02x,%02x);", (unsigned long)id, (uint8_t)a, (uint8_t)b); }
static status_$t mock_status(uint32_t id, boolean c) { logf_call("status(%lx,%02x);", (unsigned long)id, (uint8_t)c); return 0x360004; }
#include "../k_get.c"

static uint8_t out[300]; static short line; static uint16_t opts, count; static status_$t st;
static void load(const char *s, uint16_t brk)
{
    size_t n = strlen(s);
    k_reset(); memset(out, 0xEE, sizeof(out));
    tty.input_read = 1; tty.input_head = (uint16_t)(1 + n); tty.input_tail = (uint16_t)(1 + n);
    memcpy(tty.input_buffer, s, n);
    tty.break_mode = brk; tty.min_chars = 1; tty.line_id = 0x30001;
    line = 0; opts = 0; count = 100; st = 0; wait_sets_flag = 0; wait_status = 0;
}
TEST(canonical_line)
{
    load("ab\n", 0); tty.char_class['\n'] = 0x0B;
    ASSERT_EQ(3, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_EQ(0, memcmp(out, "ab\n", 3)); ASSERT_EQ(4, tty.input_read); ASSERT_EQ(0, st);
    ASSERT_EQ(0, tty.input_buffer[0]);                       /* consumed bytes cleared */
    ASSERT_STR("desc(0);lock;unlock;", call_log);
}
TEST(canonical_peek_keeps_ring)
{
    load("ab\r", 0); tty.char_class['\r'] = 0x0E; opts = 2;
    ASSERT_EQ(3, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_EQ('a', tty.input_buffer[0]); ASSERT_EQ(1, tty.input_read);
}
TEST(canonical_eof_and_tstp)
{
    load("\x04", 0); tty.char_class[4] = 0x0C;
    ASSERT_EQ(0, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_EQ(status_$tty_eof, st); ASSERT_EQ(2, tty.input_read);
    load("x\x04", 0); tty.char_class[4] = 0x0C;
    ASSERT_EQ(1, TTY_$K_GET(&line, &opts, out, &count, &st)); ASSERT_EQ(0, st);
    load("\x1a", 0); tty.char_class[0x1a] = 0x03; wait_sets_flag = 1;
    ASSERT_EQ(0, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_STR("desc(0);lock;signal(15);wait(00,0);unlock;", call_log);
}
TEST(canonical_full_buffer)
{
    load("abcd\n", 0); tty.char_class['\n'] = 0x0B; count = 2;
    ASSERT_EQ(2, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_EQ(status_$tty_buffer_full, st);
}
TEST(canonical_nothing_committed_waits)
{
    load("", 0); tty.input_tail = 3; tty.input_buffer[0] = 'q';   /* typed but not committed */
    wait_sets_flag = 1; opts = 1;
    ASSERT_EQ(0, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_STR("desc(0);lock;wait(ff,0);unlock;", call_log);
}
TEST(noncanonical_min_chars)
{
    load("abc", 1); tty.min_chars = 2;
    ASSERT_EQ(3, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_EQ(0, st); ASSERT_EQ(4, tty.input_read);
    load("a", 1); tty.min_chars = 2; wait_status = 0x350007;
    ASSERT_EQ(1, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_EQ(0x350007, st); ASSERT_STR("desc(0);lock;wait(00,1);unlock;", call_log);
    load("abc", 1); count = 3;
    ASSERT_EQ(3, TTY_$K_GET(&line, &opts, out, &count, &st)); ASSERT_EQ(0, st);
}
TEST(flow_control_reenable)
{
    int i;
    load("", 1); tty.flow_ctrl_handler = mock_flow; tty.input_flags = 0x2;
    for (i = 0; i < 0x50; i++) tty.input_buffer[i] = 'z';
    tty.input_tail = 0x51; tty.input_head = 0x51; count = 0x20;
    ASSERT_EQ(0x20, TTY_$K_GET(&line, &opts, out, &count, &st));
    ASSERT_STR("desc(0);lock;spin;flow(30001,00,ff);unspin;unlock;", call_log);
    load("", 1); tty.flow_ctrl_handler = mock_flow;
    for (i = 0; i < 0x30; i++) tty.input_buffer[i] = 'z';
    tty.input_tail = 0x31; tty.input_head = 0x31; count = 0x20;
    TTY_$K_GET(&line, &opts, out, &count, &st);
    ASSERT_STR("desc(0);lock;unlock;", call_log);              /* n < 0x40 */
}
TEST(pending_errors)
{
    load("a", 1); tty.pending_signal = 1; tty.status_handler = mock_status;
    TTY_$K_GET(&line, &opts, out, &count, &st);
    ASSERT_EQ(0x360004, st); ASSERT_EQ(0, tty.pending_signal);
    ASSERT_STR("desc(0);lock;status(30001,ff);unlock;", call_log);
    load("a", 1); tty.pending_signal = 2;
    TTY_$K_GET(&line, &opts, out, &count, &st);
    ASSERT_EQ(status_$tty_input_buffer_overrun, st);
    load("a", 1); tty.pending_signal = 0x100;
    TTY_$K_GET(&line, &opts, out, &count, &st);
    ASSERT_EQ(0, st); ASSERT_EQ(0, tty.pending_signal);
}
TEST(desc_failure_returns_zero)
{
    load("abc", 1); desc_status = 0xB000D;
    ASSERT_EQ(0, TTY_$K_GET(&line, &opts, out, &count, &st)); ASSERT_STR("desc(0);", call_log);
}
int main(void)
{
    RUN_TEST(canonical_line); RUN_TEST(canonical_peek_keeps_ring); RUN_TEST(canonical_eof_and_tstp);
    RUN_TEST(canonical_full_buffer); RUN_TEST(canonical_nothing_committed_waits);
    RUN_TEST(noncanonical_min_chars); RUN_TEST(flow_control_reenable); RUN_TEST(pending_errors);
    RUN_TEST(desc_failure_returns_zero);
    TEST_SUMMARY();
}
