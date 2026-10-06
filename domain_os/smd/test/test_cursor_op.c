/*
 * smd/test/test_cursor_op.c - smd_$cursor_op (0x00E6DFFA) through
 * SMD_$DISPLAY_CURSOR / SMD_$CLEAR_CURSOR, and smd_$reschedule_blink_timer
 * (0x00E72690)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "smd/smd_internal.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-48s %s\n", #name, current_failed ? "FAIL" : "ok");          \
    } while (0)
#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long e_ = (long long)(expected), a_ = (long long)(actual);       \
        if (e_ != a_) {                                                       \
            printf("  %s:%d: expected 0x%llx, got 0x%llx\n",                  \
                   __FILE__, __LINE__, (unsigned long long)e_,                \
                   (unsigned long long)a_);                                   \
            if (!current_failed) { current_failed = 1; tests_failed++; }      \
        }                                                                     \
    } while (0)
#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((intptr_t)(e), (intptr_t)(a))

/* ------------------------------------------------------------- globals */

smd_globals_t SMD_GLOBALS;
smd_$wired_data_t SMD_$WIRED_DATA;
smd_$blink_timer_data_t SMD_$BLINK_TIMER_DATA;
uint16_t PROC1_$AS_ID;
int16_t SMD_SYNC_LOCK_DATA = 1;
time_queue_t TIME_$RTEQ;

/* ------------------------------------------------------------- mocks */

static int acq_calls, rel_calls, xor_calls;
static int16_t *acq_lock;
static int16_t xor_num;
static uint32_t xor_pos;
static int16_t *xor_bounds;
static smd_display_hw_t *xor_hw;
static boolean xor_flag;
static uint32_t xor_base;
static SMD_HW_REG_PTR xor_regs;

uint16_t SMD_$ACQ_DISPLAY(int16_t *lock_data)
{
    acq_calls++;
    acq_lock = lock_data;
    return 0;
}

void SMD_$REL_DISPLAY(void) { rel_calls++; }

boolean SMD_$XOR_CURSOR(int16_t *cursor_num, uint32_t *cursor_pos,
                        int16_t *bounds, smd_display_hw_t *hw,
                        const boolean *erase_flag, uint32_t display_base,
                        SMD_HW_REG_PTR ctrl_regs)
{
    /* the callers' order: acquired, not yet released */
    ASSERT_EQ(1, acq_calls - rel_calls);
    xor_calls++;
    xor_num = *cursor_num;
    xor_pos = *cursor_pos;
    xor_bounds = bounds;
    xor_hw = hw;
    xor_flag = *erase_flag;
    xor_base = display_base;
    xor_regs = ctrl_regs;
    return true;
}

static clock_t abs_now = { 0x00001234, 0x5678 };
void TIME_$ABS_CLOCK(clock_t *c) { *c = abs_now; }

static int qadd_calls;
static time_queue_t *qadd_queue;
static clock_t qadd_when, qadd_now, qadd_interval;
static uint16_t qadd_abs, qadd_flags;
static void *qadd_cb, *qadd_arg;
static time_queue_elem_t *qadd_elem;
static status_$t *qadd_status;
void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, clock_t *when,
                          uint16_t is_absolute, clock_t *now,
                          void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *elem, status_$t *status)
{
    qadd_calls++;
    qadd_queue = queue; qadd_when = *when; qadd_abs = is_absolute;
    qadd_now = *now; qadd_cb = callback; qadd_arg = callback_arg;
    qadd_flags = flags; qadd_interval = *interval; qadd_elem = elem;
    qadd_status = status;
    *status = 0x12345;
}

void SMD_$BLINK_CURSOR_CALLBACK(void) { }

#include "../cursor_op.c"
#include "../display_cursor.c"
#include "../clear_cursor.c"
#include "../reschedule_blink_timer.c"

static smd_display_hw_t hw;

static void reset(void)
{
    memset(&SMD_GLOBALS, 0, sizeof(SMD_GLOBALS));
    memset(&SMD_$WIRED_DATA, 0, sizeof(SMD_$WIRED_DATA));
    memset(&hw, 0, sizeof(hw));
    PROC1_$AS_ID = 5;
    SMD_GLOBALS.asid_to_unit[5] = 1;
    smd_$unit_rec(1)->hw = &hw;
    smd_$unit_rec(1)->display_base = 0x00D00000;
    smd_$unit_rec(1)->ctrl_regs = (SMD_HW_REG_PTR)0;
    acq_calls = rel_calls = xor_calls = qadd_calls = 0;
}

TEST(display_cursor_draws_with_false_flag)
{
    uint16_t num = 2;
    smd_cursor_pos_t pos = SMD_POS_MAKE(100, 200);
    status_$t st = -1;
    reset();
    SMD_$DISPLAY_CURSOR(&num, &pos, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, xor_calls);
    ASSERT_EQ(1, rel_calls);
    ASSERT_PTR_EQ(&SMD_SYNC_LOCK_DATA, acq_lock);
    ASSERT_EQ(2, xor_num);
    ASSERT_EQ(SMD_POS_MAKE(100, 200), xor_pos);
    ASSERT_PTR_EQ(&hw.min_x, xor_bounds);
    ASSERT_PTR_EQ(&hw, xor_hw);
    ASSERT_EQ(0, xor_flag);
    ASSERT_EQ(0x00D00000, xor_base);
}

TEST(clear_cursor_passes_true_flag)
{
    uint16_t num = 0;
    smd_cursor_pos_t pos = 0;
    status_$t st = -1;
    reset();
    SMD_$CLEAR_CURSOR(&num, &pos, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(-1, xor_flag);
}

TEST(no_unit_for_asid_is_driver_misuse)
{
    uint16_t num = 0;
    smd_cursor_pos_t pos = 0;
    status_$t st = 0;
    reset();
    SMD_GLOBALS.asid_to_unit[5] = 0;
    SMD_$DISPLAY_CURSOR(&num, &pos, &st);
    ASSERT_EQ(0x00130004, st);
    ASSERT_EQ(0, acq_calls);
    ASSERT_EQ(0, xor_calls);
}

TEST(cursor_number_above_3_is_invalid)
{
    uint16_t num = 4;
    smd_cursor_pos_t pos = 0;
    status_$t st = 0;
    reset();
    SMD_$DISPLAY_CURSOR(&num, &pos, &st);
    ASSERT_EQ(0x00130023, st);
    num = 0xFFFF;                    /* bls: unsigned */
    SMD_$CLEAR_CURSOR(&num, &pos, &st);
    ASSERT_EQ(0x00130023, st);
    ASSERT_EQ(0, xor_calls);
}

TEST(reschedule_queues_relative_callback)
{
    reset();
    smd_$reschedule_blink_timer(0x0001E848);
    ASSERT_EQ(1, qadd_calls);
    ASSERT_PTR_EQ(&TIME_$RTEQ, qadd_queue);
    ASSERT_EQ(0x00000001, qadd_when.high);
    ASSERT_EQ(0xE848, qadd_when.low);
    ASSERT_EQ(0, qadd_abs);
    ASSERT_EQ(0x1234, qadd_now.high);
    ASSERT_EQ(0x5678, qadd_now.low);
    ASSERT_PTR_EQ((void *)SMD_$BLINK_CURSOR_CALLBACK, qadd_cb);
    ASSERT_PTR_EQ(NULL, qadd_arg);
    ASSERT_EQ(4, qadd_flags);
    ASSERT_EQ(0, qadd_interval.high);
    ASSERT_EQ(0x4E56, qadd_interval.low);    /* the next routine's link */
    ASSERT_PTR_EQ(&SMD_$BLINK_TIMER_DATA.qelem, qadd_elem);
}

TEST(reschedule_large_interval_keeps_high_word)
{
    reset();
    smd_$reschedule_blink_timer(0xABCD1234);
    ASSERT_EQ(0x0000ABCD, qadd_when.high);
    ASSERT_EQ(0x1234, qadd_when.low);
}

int main(void)
{
    RUN_TEST(display_cursor_draws_with_false_flag);
    RUN_TEST(clear_cursor_passes_true_flag);
    RUN_TEST(no_unit_for_asid_is_driver_misuse);
    RUN_TEST(cursor_number_above_3_is_invalid);
    RUN_TEST(reschedule_queues_relative_callback);
    RUN_TEST(reschedule_large_interval_keeps_high_word);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
