/* TTY_$K_SET/INQ_FUNC_CHAR, TTY_$K_ENABLE_FUNC, TTY_$K_INQ_FUNC_ENABLED (0x00E674D2..0x00E67654) */
#include "test_k_stubs.h"
void TTY_$I_SET_DFL_FUNCS(tty_desc_t *t, char d) { (void)t; logf_call("dfl(%02x);", (uint8_t)d); }
#include "../k_func.c"

TEST(set_func_char)
{
    short line = 0; uint16_t n = 3; char ch = 0x1c; status_$t st = 0;
    k_reset(); tty.func_enabled = 0x8; tty.func_chars[3] = 0x04; tty.char_class[0x04] = 0x0b;
    TTY_$K_SET_FUNC_CHAR(&line, &n, &ch, &st);
    ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[0x04]); ASSERT_EQ(0x1c, tty.func_chars[3]);
    ASSERT_STR("desc(0);dfl(ff);", call_log);
    k_reset(); tty.func_enabled = 0; tty.func_chars[3] = 0x04; tty.char_class[0x04] = 0x0b; tty.raw_mode = 0xFF;
    TTY_$K_SET_FUNC_CHAR(&line, &n, &ch, &st);
    ASSERT_EQ(0x0b, tty.char_class[0x04]); ASSERT_STR("desc(0);", call_log);   /* raw: no rebuild */
    n = 0x12; k_reset(); TTY_$K_SET_FUNC_CHAR(&line, &n, &ch, &st);
    ASSERT_EQ(status_$tty_invalid_function, st);
    n = 0x20; st = 0; TTY_$K_SET_FUNC_CHAR(&line, &n, &ch, &st);
    ASSERT_EQ(status_$tty_invalid_function, st);
}
TEST(inq_func_char)
{
    short line = 0; uint16_t n = 0x11; char ch = 0x55; status_$t st = 0;
    k_reset(); tty.func_chars[0x11] = 0x7e;
    TTY_$K_INQ_FUNC_CHAR(&line, &n, &ch, &st); ASSERT_EQ(0x7e, ch); ASSERT_EQ(0, st);
    n = 0x12; TTY_$K_INQ_FUNC_CHAR(&line, &n, &ch, &st); ASSERT_EQ(0, ch); ASSERT_EQ(status_$tty_invalid_function, st);
}
TEST(enable_func)
{
    short line = 0; uint16_t n = 40; char on = (char)0xFF, off = 0; status_$t st = 0; uint32_t out;
    k_reset(); TTY_$K_ENABLE_FUNC(&line, &n, &on, &st);
    ASSERT_EQ(0x100, tty.func_enabled); ASSERT_STR("desc(0);dfl(ff);", call_log);   /* 40 mod 32 */
    k_reset(); tty.func_enabled = 0xFFFF; tty.raw_mode = 0x80; n = 0;
    TTY_$K_ENABLE_FUNC(&line, &n, &off, &st);
    ASSERT_EQ(0xFFFE, tty.func_enabled); ASSERT_STR("desc(0);", call_log);
    TTY_$K_INQ_FUNC_ENABLED(&line, &out, &st); ASSERT_EQ(0xFFFE, out);
}
int main(void) { RUN_TEST(set_func_char); RUN_TEST(inq_func_char); RUN_TEST(enable_func); TEST_SUMMARY(); }
