/*
 * tty/test/test_i_echo_char.c - Unit tests for TTY_$I_ECHO_CHAR (0x00E1B3CE).
 * tty_$i_put_chars is a stub that records the bytes and the 0x1000C flags.
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

uint16_t tty_$i_put_chars(tty_desc_t *tty, const uint8_t *buf, uint32_t flags)
{
    (void)tty;
    logf_call("put(%02x,%lx);", buf[0], (unsigned long)flags);
    return 1;
}

#include "../i_echo_char.c"

static tty_desc_t tty;

static void run(uint8_t ch, uint32_t echo_flags)
{
    memset(&tty, 0, sizeof(tty));
    tty.echo_flags = echo_flags;
    log_reset();
    TTY_$I_ECHO_CHAR(&tty, ch);
}

TEST(printable_plain)
{
    run('a', 0x10);
    ASSERT_STR("put(61,1000c);", call_log);
    run(0x20, 0x10);
    ASSERT_STR("put(20,1000c);", call_log);
    run(0xE9, 0x10);
    ASSERT_STR("put(e9,1000c);", call_log);
}

TEST(tab_and_lf_plain_even_with_ctlecho)
{
    run(0x09, 0x10);
    ASSERT_STR("put(09,1000c);", call_log);
    run(0x0a, 0x10);
    ASSERT_STR("put(0a,1000c);", call_log);
}

TEST(control_without_bit4_plain)
{
    run(0x03, 0xFFFFFFEF);
    ASSERT_STR("put(03,1000c);", call_log);
    run(0x7f, 0);
    ASSERT_STR("put(7f,1000c);", call_log);
}

TEST(control_with_bit4_caret)
{
    run(0x03, 0x10);
    ASSERT_STR("put(5e,1000c);put(43,1000c);", call_log);
    run(0x0d, 0x10);
    ASSERT_STR("put(5e,1000c);put(4d,1000c);", call_log);
    run(0x1f, 0x10);
    ASSERT_STR("put(5e,1000c);put(5f,1000c);", call_log);
}

TEST(del_with_bit4_caret_query)
{
    run(0x7f, 0x10);
    ASSERT_STR("put(5e,1000c);put(3f,1000c);", call_log);
}

TEST(constant_cells)
{
    ASSERT_EQ(0x5e, tty_$echo_caret);
    ASSERT_EQ(0x3f, tty_$echo_query);
}

int main(void)
{
    printf("TTY_$I_ECHO_CHAR tests\n");
    RUN_TEST(printable_plain);
    RUN_TEST(tab_and_lf_plain_even_with_ctlecho);
    RUN_TEST(control_without_bit4_plain);
    RUN_TEST(control_with_bit4_caret);
    RUN_TEST(del_with_bit4_caret_query);
    RUN_TEST(constant_cells);
    TEST_SUMMARY();
}
