/* TTY_$K_SET/INQ_PGROUP, SET/INQ_SESSION_ID (0x00E67900..0x00E67A00) */
#include "test_k_stubs.h"
#include "../k_pgroup.c"
TEST(all)
{
    short line = 0, sid = 0x77; status_$t st = 0; uid_t u = { 0x11, 0x22 }, o = { 0, 0 };
    k_reset();
    TTY_$K_SET_PGROUP(&line, &u, &st); ASSERT_EQ(0x11, tty.pgroup_uid.high); ASSERT_EQ(0x22, tty.pgroup_uid.low);
    TTY_$K_INQ_PGROUP(&line, &o, &st); ASSERT_EQ(0x11, o.high); ASSERT_EQ(0x22, o.low);
    TTY_$K_SET_SESSION_ID(&line, &sid, &st); ASSERT_EQ(0x77, tty.session_id);
    sid = 0; TTY_$K_INQ_SESSION_ID(&line, &sid, &st); ASSERT_EQ(0x77, sid);
    desc_status = 4; u.high = 9; TTY_$K_SET_PGROUP(&line, &u, &st); ASSERT_EQ(0x11, tty.pgroup_uid.high); ASSERT_EQ(4, st);
}
int main(void) { RUN_TEST(all); TEST_SUMMARY(); }
