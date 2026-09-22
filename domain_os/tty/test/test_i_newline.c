/*
 * tty/test/test_i_newline.c - Unit tests for TTY_$I_NEWLINE (0x00E1B456).
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

uint16_t tty_$i_put_chars(tty_desc_t *tty, const uint8_t *buf, uint32_t flags)
{ (void)tty; logf_call("put(%02x,%lx);", buf[0], (unsigned long)flags); return 1; }

#include "../i_newline.c"

static tty_desc_t tty;

static void run(uint32_t oflags)
{
    memset(&tty, 0, sizeof(tty));
    tty.output_flags = oflags;
    log_reset();
    TTY_$I_NEWLINE(&tty);
}

TEST(neither_bit_lf_then_cr)
{
    run(0);
    ASSERT_STR("put(0a,1000c);put(0d,1000c);", call_log);
    run(0xFFFFFFFC);
    ASSERT_STR("put(0a,1000c);put(0d,1000c);", call_log);
}

TEST(bit0_cr_only)
{
    run(0x1);
    ASSERT_STR("put(0d,1000c);", call_log);
    run(0x3);                                /* bit 0 wins over bit 1 */
    ASSERT_STR("put(0d,1000c);", call_log);
}

TEST(bit1_lf_only)
{
    run(0x2);
    ASSERT_STR("put(0a,1000c);", call_log);
}

TEST(cells)
{
    ASSERT_EQ(0x0a, tty_$newline_lf);
    ASSERT_EQ(0x0d, tty_$newline_cr);
}

int main(void)
{
    printf("TTY_$I_NEWLINE tests\n");
    RUN_TEST(neither_bit_lf_then_cr);
    RUN_TEST(bit0_cr_only);
    RUN_TEST(bit1_lf_only);
    RUN_TEST(cells);
    TEST_SUMMARY();
}
