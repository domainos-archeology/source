/* TTY_$K_SIMULATE_TERMINAL_INPUT (0x00E1C148) */
#include "test_k_stubs.h"
#include "proc2/proc2.h"
static uint16_t my_upid; static status_$t upgid_status; static uid_t my_uid;
void PROC2_$GET_MY_UPIDS(uint16_t *upid, uint16_t *uppid, uint16_t *upgid) { *upid = my_upid; *uppid = 1; upgid[0] = 5; upgid[1] = 6; upgid[2] = 7; }
void PROC2_$UPGID_TO_UID(uint16_t *upgid, uid_t *u, status_$t *st) { ASSERT_EQ(5, upgid[0]); *u = my_uid; *st = upgid_status; }
void TTY_$I_RCV(tty_desc_t *t, uint8_t ch) { (void)t; logf_call("rcv(%02x);", ch); }
#include "../k_simulate.c"
TEST(owner_or_root)
{
    short line = 0; char ch = 'q'; status_$t st = 0;
    k_reset(); my_upid = 7; my_uid.high = 1; my_uid.low = 2; upgid_status = 0; tty.pgroup_uid.high = 1; tty.pgroup_uid.low = 2;
    TTY_$K_SIMULATE_TERMINAL_INPUT(&line, &ch, &st);
    ASSERT_STR("desc(0);spin;rcv(71);unspin;", call_log); ASSERT_EQ(0, st);
    k_reset(); tty.pgroup_uid.low = 3;
    TTY_$K_SIMULATE_TERMINAL_INPUT(&line, &ch, &st);
    ASSERT_EQ(status_$tty_invalid_option, st); ASSERT_STR("desc(0);", call_log);
    k_reset(); my_upid = 0; st = 0;
    TTY_$K_SIMULATE_TERMINAL_INPUT(&line, &ch, &st);
    ASSERT_STR("desc(0);spin;rcv(71);unspin;", call_log);
    k_reset(); upgid_status = 0x12; TTY_$K_SIMULATE_TERMINAL_INPUT(&line, &ch, &st);
    ASSERT_EQ(0x12, st); ASSERT_STR("desc(0);", call_log);
}
int main(void) { RUN_TEST(owner_or_root); TEST_SUMMARY(); }
