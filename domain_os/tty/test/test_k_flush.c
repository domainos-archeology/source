/* TTY_$K_FLUSH_INPUT / OUTPUT (0x00E1C084 / 0x00E1C0E6) */
#include "test_k_stubs.h"
void TTY_$I_FLUSH_INPUT(tty_desc_t *t) { (void)t; logf_call("fin;"); }
void TTY_$I_FLUSH_OUTPUT(tty_desc_t *t) { (void)t; logf_call("fout;"); }
#include "../k_flush.c"
TEST(both)
{
    short line = 1; status_$t st = 0;
    k_reset(); TTY_$K_FLUSH_INPUT(&line, &st); ASSERT_STR("desc(1);spin;fin;unspin;", call_log);
    k_reset(); TTY_$K_FLUSH_OUTPUT(&line, &st); ASSERT_STR("desc(1);spin;fout;unspin;", call_log);
    k_reset(); desc_status = 9; TTY_$K_FLUSH_OUTPUT(&line, &st); ASSERT_STR("desc(1);", call_log); ASSERT_EQ(9, st);
}
int main(void) { RUN_TEST(both); TEST_SUMMARY(); }
