/* TTY_$K_SET/INQ_INPUT_BREAK_MODE (0x00E6781E / 0x00E678BC) */
#include "test_k_stubs.h"
uint32_t DAT_00e82454 = 0xD0;
static uint32_t funcs_mask; static int funcs_dfl;
void tty_$i_set_funcs(tty_desc_t *t, uint32_t m, char d) { (void)t; funcs_mask = m; funcs_dfl = (uint8_t)d; logf_call("funcs;"); }
#include "../k_break.c"

TEST(set_mode_zero_reenables_crash)
{
    short line = 1; status_$t st = 0;
    break_mode_t m = { 0, 7, 0x11223344 };
    k_reset(); tty.crash_char = 0x1c;
    TTY_$K_SET_INPUT_BREAK_MODE(&line, &m, &st);
    ASSERT_EQ(0, tty.break_mode); ASSERT_EQ(7, tty.min_chars); ASSERT_EQ(0x11223344, tty.reserved_3C);
    ASSERT_EQ(0xD0, funcs_mask); ASSERT_EQ(0xFF, funcs_dfl);
    ASSERT_EQ(TTY_CHAR_CLASS_CRASH, tty.char_class[0x1c]);
}
TEST(set_mode_nonzero_disables)
{
    short line = 1; status_$t st = 0;
    break_mode_t m = { 3, 1, 0 };
    k_reset(); tty.crash_char = 0x1c; tty.char_class[0x1c] = TTY_CHAR_CLASS_CRASH;
    TTY_$K_SET_INPUT_BREAK_MODE(&line, &m, &st);
    ASSERT_EQ(0, funcs_dfl);
    ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[0x1c]);
    k_reset(); tty.crash_char = 0; tty.char_class[0] = 0x55;
    TTY_$K_SET_INPUT_BREAK_MODE(&line, &m, &st);
    ASSERT_EQ(0x55, tty.char_class[0]);
}
TEST(desc_failure)
{
    short line = 1; status_$t st = 0; break_mode_t m = { 1, 1, 1 };
    k_reset(); desc_status = 0xB000D;
    TTY_$K_SET_INPUT_BREAK_MODE(&line, &m, &st);
    ASSERT_EQ(0xB000D, st); ASSERT_EQ(0, tty.break_mode); ASSERT_STR("desc(1);", call_log);
}
TEST(inq)
{
    short line = 2; status_$t st = 0; break_mode_t m = { 9, 9, 9 };
    k_reset(); tty.break_mode = 2; tty.min_chars = 5; tty.reserved_3C = 0xABCD;
    TTY_$K_INQ_INPUT_BREAK_MODE(&line, &m, &st);
    ASSERT_EQ(2, m.mode); ASSERT_EQ(5, m.min_chars); ASSERT_EQ(0xABCD, m.reserved);
}
int main(void)
{
    RUN_TEST(set_mode_zero_reenables_crash); RUN_TEST(set_mode_nonzero_disables);
    RUN_TEST(desc_failure); RUN_TEST(inq);
    TEST_SUMMARY();
}
