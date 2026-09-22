/* TTY_$K_RESET (0x00E672DE) */
#include "test_k_stubs.h"
uid_t UID_$NIL = { 0xAAAA, 0xBBBB };
static void mock_xon(uint32_t id, boolean s) { logf_call("xon(%lx,%02x);", (unsigned long)id, (uint8_t)s); }
static void mock_flow(uint32_t id, boolean a, boolean b) { logf_call("flow(%lx,%02x,%02x);", (unsigned long)id, (uint8_t)a, (uint8_t)b); }
#include "../k_reset.c"
TEST(reset)
{
    short line = 0; status_$t st = 0; int i;
    k_reset(); memset(&tty, 0x55, sizeof(tty));
    tty.line_id = 0x30002; tty.input_flags = 0x2; tty.xon_xoff_handler = mock_xon; tty.flow_ctrl_handler = mock_flow;
    TTY_$K_RESET(&line, &st);
    ASSERT_EQ(1, tty.input_head); ASSERT_EQ(1, tty.input_read); ASSERT_EQ(1, tty.input_tail); ASSERT_EQ(0x100, tty.input_size);
    ASSERT_EQ(1, tty.output_head); ASSERT_EQ(1, tty.output_read); ASSERT_EQ(0x100, tty.output_tail);
    ASSERT_EQ(0, tty.saved_input_flags); ASSERT_EQ(0, tty.column); ASSERT_EQ(0, tty.pending_signal);
    ASSERT_EQ(0xAAAA, tty.pgroup_uid.high); ASSERT_EQ(0xBBBB, tty.pgroup_uid.low);
    ASSERT_EQ(0, tty.session_id); ASSERT_EQ(0, tty.state_flags);
    for (i = 0; i < 5; i++) ASSERT_EQ(0, tty.delay[i]);
    ASSERT_EQ(0x5555, tty.reserved_4A); ASSERT_EQ(0x2, tty.input_flags);
    ASSERT_STR("desc(0);lock;xon(30002,00);flow(30002,00,ff);unlock;", call_log);
    k_reset(); TTY_$K_RESET(&line, &st); ASSERT_STR("desc(0);lock;unlock;", call_log);
}
int main(void) { RUN_TEST(reset); TEST_SUMMARY(); }
