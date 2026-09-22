/* TTY_$K_SET_DELAY / INQ_DELAY (0x00E67A02 / 0x00E67A58), TTY_$K_DRAIN_OUTPUT (0x00E67AAE) */
#include "test_k_stubs.h"
#include "proc1/proc1.h"
#include "fim/fim.h"
uint16_t PROC1_$AS_ID;
ec_$eventcount_t FIM_$QUIT_EC[8];
uint32_t FIM_$QUIT_VALUE[8];
static ec_$eventcount_t out_ec;
static int waitn_results[4]; static int waitn_calls;
static int32_t waitn_vals[2]; static ec_$eventcount_t *waitn_ecs[2];
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{
    waitn_ecs[0] = ecs[0]; waitn_ecs[1] = ecs[1]; waitn_vals[0] = vals[0]; waitn_vals[1] = vals[1];
    logf_call("waitn(%d);", n);
    if (waitn_results[waitn_calls] == 1) tty.output_read = tty.output_head;   /* drained */
    return (uint16_t)waitn_results[waitn_calls++];
}
#include "../k_delay.c"

TEST(delay_validation_and_index)
{
    short line = 0; status_$t st = 0; ushort t; short v = 0x123;
    for (t = 0; t < 5; t++) {
        k_reset(); st = 0; v = 0x123;
        TTY_$K_SET_DELAY(&line, &t, &v, &st);
        ASSERT_EQ(0, st); ASSERT_EQ(0x123, tty.delay[t]);
        tty.delay[t] = (short)(0x300 + t); v = 0;
        TTY_$K_INQ_DELAY(&line, &t, &v, &st);
        ASSERT_EQ(0x300 + t, v);
    }
    t = 5; k_reset(); TTY_$K_SET_DELAY(&line, &t, &v, &st);
    ASSERT_EQ(status_$tty_invalid_option, st); ASSERT_STR("", call_log);
    t = 32; k_reset(); st = 0; TTY_$K_INQ_DELAY(&line, &t, &v, &st);   /* bit 32 mod 32 = 0 */
    ASSERT_EQ(0, st); ASSERT_STR("desc(0);", call_log);
}
TEST(drain_already_empty)
{
    short line = 0; status_$t st = 0;
    /* the image reads *output_ec (0x00E67B12) before testing emptiness */
    k_reset(); ARCH_HOST_VA_BASE = (uintptr_t)&out_ec - 0x1000;
    tty.output_head = 4; tty.output_read = 4; tty.output_ec = ARCH_PTR_TO_VA(&out_ec);
    TTY_$K_DRAIN_OUTPUT(&line, &st);
    ASSERT_STR("desc(0);lock;unlock;", call_log); ASSERT_EQ(0, st);
}
TEST(drain_waits_then_done)
{
    short line = 0; status_$t st = 0;
    k_reset(); waitn_calls = 0; waitn_results[0] = 1;
    /* output_ec is a 32-bit VA: anchor the host arena on out_ec */
    ARCH_HOST_VA_BASE = (uintptr_t)&out_ec - 0x1000;
    tty.output_head = 1; tty.output_read = 5; tty.output_ec = ARCH_PTR_TO_VA(&out_ec);
    out_ec.value = 41; PROC1_$AS_ID = 3; FIM_$QUIT_VALUE[3] = 10;
    TTY_$K_DRAIN_OUTPUT(&line, &st);
    ASSERT_STR("desc(0);lock;unlock;waitn(2);lock;unlock;", call_log);
    ASSERT_EQ((unsigned long)&out_ec, (unsigned long)waitn_ecs[0]); ASSERT_EQ(42, waitn_vals[0]);
    ASSERT_EQ((unsigned long)&FIM_$QUIT_EC[3], (unsigned long)waitn_ecs[1]); ASSERT_EQ(11, waitn_vals[1]);
    ASSERT_EQ(0, st);
}
TEST(drain_quit)
{
    short line = 0; status_$t st = 0;
    k_reset(); waitn_calls = 0; waitn_results[0] = 2;
    ARCH_HOST_VA_BASE = (uintptr_t)&out_ec - 0x1000;
    tty.output_head = 1; tty.output_read = 5; tty.output_ec = ARCH_PTR_TO_VA(&out_ec);
    PROC1_$AS_ID = 2; FIM_$QUIT_EC[2].value = 77; FIM_$QUIT_VALUE[2] = 5;
    TTY_$K_DRAIN_OUTPUT(&line, &st);
    ASSERT_EQ(status_$tty_quit_while_waiting_for_input, st);
    ASSERT_EQ(77, FIM_$QUIT_VALUE[2]);
    ASSERT_STR("desc(0);lock;unlock;waitn(2);lock;unlock;", call_log);
}
int main(void)
{
    RUN_TEST(delay_validation_and_index); RUN_TEST(drain_already_empty);
    RUN_TEST(drain_waits_then_done); RUN_TEST(drain_quit);
    TEST_SUMMARY();
}
