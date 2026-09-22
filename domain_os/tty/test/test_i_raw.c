/* tty/test/test_i_raw.c - TTY_$I_SET_RAW (0x00E673AA), TTY_$I_INQ_RAW
 * (0x00E673EA), TTY_$I_ENABLE_CRASH_FUNC (0x00E67292). */
#include "tty/tty_internal.h"
#include "test_harness.h"

static tty_desc_t tty;
static status_$t desc_status;
tty_desc_t *TTY_$I_GET_DESC(short line, status_$t *status)
{ logf_call("desc(%d);", line); *status = desc_status; return &tty; }
void TTY_$I_SET_RAW_MODE(tty_desc_t *t, char raw)
{ (void)t; logf_call("rawmode(%02x);", (uint8_t)raw); }

#include "../i_raw.c"

static void reset(void)
{
    memset(&tty, 0, sizeof(tty));
    desc_status = status_$ok;
    log_reset();
}

TEST(set_raw)
{
    status_$t st = 0;
    reset();
    TTY_$I_SET_RAW(2, (char)0xFF, &st);
    ASSERT_STR("desc(2);rawmode(ff);", call_log);

    reset();
    desc_status = 0x000b000d;
    TTY_$I_SET_RAW(2, (char)0xFF, &st);
    ASSERT_STR("desc(2);", call_log);
    ASSERT_EQ(0x000b000d, st);
}

TEST(inq_raw)
{
    status_$t st = 0;
    char raw = 0x55;
    reset();
    tty.raw_mode = 0xFF;
    TTY_$I_INQ_RAW(1, &raw, &st);
    ASSERT_EQ(0xFF, (uint8_t)raw);

    reset();
    desc_status = 0x000b0007;
    raw = 0x55;
    TTY_$I_INQ_RAW(1, &raw, &st);
    ASSERT_EQ(0x55, (uint8_t)raw);
}

TEST(enable_crash)
{
    reset();
    tty.char_class[0x1c] = TTY_CHAR_CLASS_NORMAL;
    TTY_$I_ENABLE_CRASH_FUNC(&tty, 0x1c, (char)0xFF);
    ASSERT_EQ(0x1c, tty.crash_char);
    ASSERT_EQ(TTY_CHAR_CLASS_CRASH, tty.char_class[0x1c]);
}

TEST(disable_crash_uses_argument_char)
{
    reset();
    tty.crash_char = 0x1c;
    tty.char_class[0x1c] = TTY_CHAR_CLASS_CRASH;
    tty.char_class[0x1d] = 0x33;
    TTY_$I_ENABLE_CRASH_FUNC(&tty, 0x1d, 0);
    ASSERT_EQ(0, tty.crash_char);
    ASSERT_EQ(TTY_CHAR_CLASS_CRASH, tty.char_class[0x1c]);   /* untouched */
    ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[0x1d]);

    reset();
    tty.char_class[0x1d] = 0x33;
    TTY_$I_ENABLE_CRASH_FUNC(&tty, 0x1d, 0x7f);              /* none set */
    ASSERT_EQ(0x33, tty.char_class[0x1d]);
    ASSERT_EQ(0, tty.crash_char);
}

int main(void)
{
    printf("TTY_$I_*RAW / ENABLE_CRASH_FUNC tests\n");
    RUN_TEST(set_raw);
    RUN_TEST(inq_raw);
    RUN_TEST(enable_crash);
    RUN_TEST(disable_crash_uses_argument_char);
    TEST_SUMMARY();
}
