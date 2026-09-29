/* tty_$i_wait (0x00E1C204): the EC_$WAITN argument build, the four
 * break-mode timer decisions, the no-wait path, and the quit / input / timer
 * outcomes with the TIME_$CANCEL cleanup. */
#include "tty/tty_internal.h"
#include "test_harness.h"
#include "proc1/proc1.h"
#include "fim/fim.h"
#include "ec/ec.h"
#include "time/time.h"
#include "cal/cal.h"
#include "math/math.h"

static tty_desc_t tty;
uint16_t PROC1_$AS_ID;
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);
static ec_$eventcount_t in_ec;

void TTY_$I_LOCK(tty_desc_t *t) { (void)t; logf_call("lock;"); }
void TTY_$I_UNLOCK(tty_desc_t *t) { (void)t; logf_call("unlock;"); }

static uint16_t waitn_result;
static int16_t waitn_n;
static ec_$eventcount_t *waitn_ecs[3];
static int32_t waitn_vals[3];
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{
    int i;
    waitn_n = n;
    for (i = 0; i < 3; i++) { waitn_ecs[i] = ecs[i]; waitn_vals[i] = vals[i]; }
    logf_call("waitn(%d);", n);
    return waitn_result;
}
void EC_$INIT(ec_$eventcount_t *ec) { ec->value = 7; logf_call("ecinit;"); }

static clock_t clock_now;
void TIME_$CLOCK(clock_t *c) { *c = clock_now; logf_call("clock;"); }

static clock_t adv_when; static uint16_t adv_abs; static ec_$eventcount_t *adv_ec;
static time_queue_elem_t *adv_elem;
void TIME_$ADVANCE(uint16_t *is_absolute, clock_t *when, ec_$eventcount_t *ec,
                   time_queue_elem_t *elem, status_$t *status)
{
    adv_abs = *is_absolute; adv_when = *when; adv_ec = ec; adv_elem = elem;
    *status = 0;
    logf_call("advance(%lx:%x);", (unsigned long)when->high, when->low);
}
static int32_t cancel_val; static time_queue_elem_t *cancel_elem;
void TIME_$CANCEL(int32_t wait_value, time_queue_elem_t *elem, status_$t *status)
{
    cancel_val = wait_value; cancel_elem = elem; *status = 0;
    logf_call("cancel(%ld);", (long)wait_value);
}
/* dst -= src on the 48-bit value */
int8_t SUB48(clock_t *dst, clock_t *src)
{
    unsigned long long d = ((unsigned long long)dst->high << 16) | dst->low;
    unsigned long long s = ((unsigned long long)src->high << 16) | src->low;
    d -= s;
    dst->high = (uint)(d >> 16); dst->low = (ushort)(d & 0xffff);
    logf_call("sub48;");
    return (int8_t)-1;
}
ulong M$MIU$LLW(ulong a, ushort b) { logf_call("mul(%lu,%u);", a, b); return a * b; }

#include "../i_wait.c"

static char done; static status_$t st;
static void reset(void)
{
    memset(&tty, 0, sizeof(tty)); log_reset();
    ARCH_HOST_VA_BASE = (uintptr_t)&in_ec - 0x1000;
    tty.input_ec = ARCH_PTR_TO_VA(&in_ec); in_ec.value = 20;
    PROC1_$AS_ID = 2; FIM_$WIRED_DATA.quit_ec[2].value = 30; FIM_$WIRED_DATA.quit_value[2] = 30;
    done = 0; st = 0; waitn_result = 2; waitn_n = 0;
    clock_now.high = 0; clock_now.low = 0;
}

TEST(mode0_waits_on_two_ecs_and_input_wakes)
{
    reset();
    tty_$i_wait(&tty, 0, &done, 0, &st);
    ASSERT_STR("unlock;waitn(2);lock;", call_log);
    ASSERT_EQ(2, waitn_n);
    ASSERT_EQ((unsigned long)&FIM_$WIRED_DATA.quit_ec[2], (unsigned long)waitn_ecs[0]);
    ASSERT_EQ(31, waitn_vals[0]);
    ASSERT_EQ((unsigned long)&in_ec, (unsigned long)waitn_ecs[1]);
    ASSERT_EQ(21, waitn_vals[1]);
    ASSERT_EQ(0, done); ASSERT_EQ(0, st);
}
TEST(quit_wakes)
{
    reset(); waitn_result = 1; FIM_$WIRED_DATA.quit_ec[2].value = 44;
    tty_$i_wait(&tty, 0, &done, 3, &st);
    ASSERT_EQ(status_$tty_quit_while_waiting_for_input, st);
    ASSERT_EQ(44, FIM_$WIRED_DATA.quit_value[2]);
    ASSERT_EQ(0, done);
}
TEST(no_wait_flag)
{
    reset();
    tty_$i_wait(&tty, (char)0xFF, &done, 0, &st);
    ASSERT_STR("", call_log);
    ASSERT_EQ(status_$tty_get_conditional_failed, st);
    reset();
    tty_$i_wait(&tty, (char)0xFF, &done, 2, &st);      /* count != 0: no status */
    ASSERT_STR("", call_log); ASSERT_EQ(0, st);
}
TEST(mode2_arms_relative_timer_checks)
{
    reset(); tty.break_mode = 2; tty.reserved_3C = 3;
    tty_$i_wait(&tty, 0, &done, 0, &st);
    ASSERT_STR("ecinit;mul(3,25000);advance(1:24f8);unlock;waitn(3);lock;cancel(8);", call_log);
    ASSERT_EQ(0, adv_abs);
    ASSERT_EQ(3, waitn_n);
    ASSERT_EQ((unsigned long)adv_ec, (unsigned long)waitn_ecs[2]);
    ASSERT_EQ(8, waitn_vals[2]);                       /* EC_$INIT value 7 + 1 */
    ASSERT_EQ((unsigned long)adv_elem, (unsigned long)cancel_elem);
    ASSERT_EQ(0, done); ASSERT_EQ(0, st);
}
TEST(mode3_timer_fires_sets_done_no_cancel)
{
    reset(); tty.break_mode = 3; tty.reserved_3C = 4;   /* 100000 = 0x186a0 */
    tty.last_input_clock_high = 0; tty.last_input_clock_low = 0x100;
    clock_now.high = 0; clock_now.low = 0x180;         /* elapsed 0x80 */
    waitn_result = 3;
    tty_$i_wait(&tty, 0, &done, 1, &st);
    /* timeout - (now - last) = 0x186a0 - 0x80 = 0x18620 */
    ASSERT_STR("ecinit;mul(4,25000);clock;sub48;sub48;advance(1:8620);unlock;waitn(3);lock;", call_log);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)done); ASSERT_EQ(0, st);
}
TEST(mode3_count_zero_has_no_timer)
{
    reset(); tty.break_mode = 3;
    tty_$i_wait(&tty, 0, &done, 0, &st);
    ASSERT_STR("unlock;waitn(2);lock;", call_log);
}
TEST(mode1_and_mode33_take_the_bit_test)
{
    reset(); tty.break_mode = 1;
    tty_$i_wait(&tty, 0, &done, 5, &st);
    ASSERT_STR("unlock;waitn(2);lock;", call_log);
    reset(); tty.break_mode = 33;                       /* bit 33 mod 32 = bit 1 of 3 */
    tty_$i_wait(&tty, 0, &done, 5, &st);
    ASSERT_STR("unlock;waitn(2);lock;", call_log);
}

int main(void)
{
    printf("tty_$i_wait tests\n");
    RUN_TEST(mode0_waits_on_two_ecs_and_input_wakes);
    RUN_TEST(quit_wakes);
    RUN_TEST(no_wait_flag);
    RUN_TEST(mode2_arms_relative_timer_checks);
    RUN_TEST(mode3_timer_fires_sets_done_no_cancel);
    RUN_TEST(mode3_count_zero_has_no_timer);
    RUN_TEST(mode1_and_mode33_take_the_bit_test);
    TEST_SUMMARY();
}
