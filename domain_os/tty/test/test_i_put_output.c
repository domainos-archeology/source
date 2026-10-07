/* tty/test/test_i_put_output.c - Unit tests for TTY_$I_PUT_OUTPUT (0x00E1BF0E). */
#include "tty/tty_internal.h"
#include "test_harness.h"

uint32_t TTY_$SPIN_LOCK;
static int lock_calls, unlock_calls;
ml_$spin_token_t ML_$SPIN_LOCK(void *lockp) { (void)lockp; lock_calls++; return 7; }
void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t t_slot) { ml_$spin_token_t t = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(t_slot); (void)t; (void)lockp; (void)t; unlock_calls++; }

static uint32_t flags_seen;
uint16_t tty_$i_put_chars(tty_desc_t *tty, const uint8_t *buf, uint32_t flags)
{ (void)tty; flags_seen = flags; logf_call("put(%02x);", buf[0]); return 5; }

#include "../i_put_output.c"

static tty_desc_t tty;
static uint8_t data[4] = { 'x', 'y', 'z', 0 };

static void reset(void)
{
    memset(&tty, 0, sizeof(tty));
    lock_calls = unlock_calls = 0;
    flags_seen = 0;
    log_reset();
}

TEST(passes_through_when_bit6_clear)
{
    reset();
    tty.input_tail = 5; tty.input_head = 1;
    ASSERT_EQ(5, TTY_$I_PUT_OUTPUT(&tty, data, 3, 0xC));
    ASSERT_STR("put(78);", call_log);
    ASSERT_EQ(0x0003000C, flags_seen);
    ASSERT_EQ(0, lock_calls);
}

TEST(passes_through_when_no_pending_input)
{
    reset();
    tty.input_flags = 0x40;
    tty.input_tail = 4; tty.input_head = 4;
    ASSERT_EQ(5, TTY_$I_PUT_OUTPUT(&tty, data, 1, 0));
    ASSERT_EQ(0x00010000, flags_seen);
    ASSERT_EQ(0, tty.state_flags);
}

TEST(defers_with_pending_input)
{
    reset();
    tty.input_flags = 0x40;
    tty.input_tail = 5; tty.input_head = 1;
    tty.state_flags = 0x0100;
    ASSERT_EQ(0, TTY_$I_PUT_OUTPUT(&tty, data, 3, 0));
    ASSERT_STR("", call_log);
    ASSERT_EQ(0x0102, tty.state_flags);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
}

int main(void)
{
    printf("TTY_$I_PUT_OUTPUT tests\n");
    RUN_TEST(passes_through_when_bit6_clear);
    RUN_TEST(passes_through_when_no_pending_input);
    RUN_TEST(defers_with_pending_input);
    TEST_SUMMARY();
}
