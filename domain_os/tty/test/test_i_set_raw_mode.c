/* tty/test/test_i_set_raw_mode.c - TTY_$I_SET_RAW_MODE (0x00E1BF70) with the
 * real masks from tty_data.c (0x1F output, 0x3C input). */
#include "tty/tty_internal.h"
#include "test_harness.h"

void TTY_$I_DXM_SIGNAL(tty_signal_entry_t **e) { (void)e; }
dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn) { return (dxm_$callback_t)(fn != NULL); }
static int lock_depth;
ml_$spin_token_t ML_$SPIN_LOCK(void *lockp) { (void)lockp; lock_depth++; logf_call("lock;"); return 1; }
void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t t_slot) { ml_$spin_token_t t = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(t_slot); (void)t; (void)lockp; (void)t; lock_depth--; logf_call("unlock;"); }
void TTY_$I_ADVANCE_EC(m68k_ptr_t ec) { logf_call("advance(%lx);", (unsigned long)ec); }
void TTY_$I_SET_DFL_FUNCS(tty_desc_t *t, char use_dfl) { (void)t; logf_call("dfl(%02x);", (uint8_t)use_dfl); }

#include "../tty_data.c"
#include "../i_set_raw_mode.c"

static tty_desc_t tty;

static void reset(void)
{
    memset(&tty, 0, sizeof(tty));
    tty.input_flags = 0xFFFF;
    tty.output_flags = 0xFF;
    tty.output_ec = 0x2222;
    tty.break_mode = 0; tty.min_chars = 4;
    log_reset();
}

TEST(enter_raw)
{
    reset();
    tty.state_flags = 0x0105;
    tty.crash_char = 0x1c; tty.char_class[0x1c] = TTY_CHAR_CLASS_CRASH;
    TTY_$I_SET_RAW_MODE(&tty, (char)0xFF);
    ASSERT_EQ(0x3C, tty.raw_saved_input_flags);
    ASSERT_EQ(0xFFC3, tty.input_flags);
    ASSERT_EQ(0x1F, tty.raw_saved_output_flags);
    ASSERT_EQ(0xE0, tty.output_flags);
    ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[0x1c]);
    ASSERT_EQ(1, tty.break_mode);
    ASSERT_EQ(1, tty.min_chars);
    ASSERT_EQ(0x0100, tty.state_flags);
    ASSERT_EQ(0xFF, tty.raw_mode);
    ASSERT_STR("dfl(00);lock;advance(2222);unlock;", call_log);
}

TEST(enter_raw_no_wake_when_low_bits_clear)
{
    reset();
    tty.state_flags = 0x0108;
    TTY_$I_SET_RAW_MODE(&tty, (char)0x80);
    ASSERT_EQ(0x0108, tty.state_flags);
    ASSERT_STR("dfl(00);lock;unlock;", call_log);
}

TEST(enter_raw_twice_is_noop)
{
    reset();
    tty.raw_mode = 0xFF;
    TTY_$I_SET_RAW_MODE(&tty, (char)0xFF);
    ASSERT_EQ(0xFFFF, tty.input_flags);
    ASSERT_STR("", call_log);
}

TEST(leave_raw)
{
    reset();
    tty.raw_mode = 0xFF;
    tty.input_flags = 0xFFC3; tty.raw_saved_input_flags = 0x30;
    tty.output_flags = 0xE0; tty.raw_saved_output_flags = 0x05;
    tty.break_mode = 1; tty.min_chars = 1;
    tty.crash_char = 0x1c;
    TTY_$I_SET_RAW_MODE(&tty, 0);
    ASSERT_EQ(0xFFF3, tty.input_flags);
    ASSERT_EQ(0xE5, tty.output_flags);
    ASSERT_EQ(0, tty.raw_saved_input_flags);
    ASSERT_EQ(0, tty.raw_saved_output_flags);
    ASSERT_EQ(0, tty.break_mode);
    ASSERT_EQ(1, tty.min_chars);                     /* untouched */
    ASSERT_EQ(TTY_CHAR_CLASS_CRASH, tty.char_class[0x1c]);
    ASSERT_EQ(0, tty.raw_mode);
    ASSERT_STR("dfl(ff);", call_log);
}

TEST(leave_raw_when_not_raw_is_noop)
{
    reset();
    tty.raw_mode = 0;
    tty.raw_saved_input_flags = 0x30;
    TTY_$I_SET_RAW_MODE(&tty, 0x7f);
    ASSERT_EQ(0x30, tty.raw_saved_input_flags);
    ASSERT_STR("", call_log);
}

int main(void)
{
    printf("TTY_$I_SET_RAW_MODE tests\n");
    RUN_TEST(enter_raw);
    RUN_TEST(enter_raw_no_wake_when_low_bits_clear);
    RUN_TEST(enter_raw_twice_is_noop);
    RUN_TEST(leave_raw);
    RUN_TEST(leave_raw_when_not_raw_is_noop);
    TEST_SUMMARY();
}
