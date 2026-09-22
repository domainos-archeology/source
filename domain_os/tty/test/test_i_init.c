/*
 * tty/test/test_i_init.c - Unit tests for TTY_$I_INIT (0x00E3324C).
 * Pins every field the routine writes, the two image tables at 0x00E351B0,
 * and the TTY_$I_SET_DFL_FUNCS(tty, true) call.
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

uid_t UID_$NIL = { 0x11111111, 0x22222222 };

static tty_desc_t *dfl_tty;
static int dfl_use;
void TTY_$I_SET_DFL_FUNCS(tty_desc_t *tty, char use_dfl)
{
    dfl_tty = tty;
    dfl_use = (uint8_t)use_dfl;
    logf_call("dfl;");
}

#include "../i_init.c"

static tty_desc_t tty;

TEST(image_tables)
{
    static const uint8_t chars[18] = { 0x08,0x17,0x15,0x04,0x00,0x09,0x0d,0x0a,0x1c,
                                       0x03,0x1a,0x19,0x16,0x13,0x11,0x0f,0x12,0x00 };
    ASSERT_EQ(0, memcmp(chars, tty_$i_init_data.func_chars, 18));
    ASSERT_EQ(0x00120010, tty_$i_init_data.signal_status[0]);
    ASSERT_EQ(0x0012001F, tty_$i_init_data.signal_status[1]);
    ASSERT_EQ(0x00120028, tty_$i_init_data.signal_status[2]);
    ASSERT_EQ(0x000B000E, tty_$i_init_data.signal_status[3]);
    ASSERT_EQ(0, tty_$i_init_data.signal_status[4]);
    ASSERT_EQ(0, tty_$i_init_data.signal_status[5]);
    ASSERT_EQ(0x1A, tty_$i_init_data.signal_num[4]);
    ASSERT_EQ(0x16, tty_$i_init_data.signal_num[5]);
}

TEST(scalar_fields)
{
    memset(&tty, 0xAA, sizeof(tty));
    log_reset();
    TTY_$I_INIT(&tty);
    ASSERT_EQ(0, tty.state_flags);
    ASSERT_EQ(0x29, tty.input_flags);
    ASSERT_EQ(2, tty.output_flags);
    ASSERT_EQ(0x23, tty.echo_flags);
    ASSERT_EQ(0, tty.raw_saved_input_flags);
    ASSERT_EQ(0, tty.raw_saved_output_flags);
    ASSERT_EQ(0, tty.crash_char);
    ASSERT_EQ(0x1FFEF, tty.func_enabled);
    ASSERT_EQ(0, tty.break_mode);
    ASSERT_EQ(0x11111111, tty.pgroup_uid.high);
    ASSERT_EQ(0x22222222, tty.pgroup_uid.low);
    ASSERT_EQ(0, tty.session_id);
    ASSERT_EQ(0, tty.column);
    ASSERT_EQ(0, tty.saved_input_flags);
    ASSERT_EQ(0, tty.pending_signal);
    ASSERT_EQ(0, tty.raw_mode);
    ASSERT_EQ(1, tty.input_head);
    ASSERT_EQ(1, tty.input_read);
    ASSERT_EQ(1, tty.input_tail);
    ASSERT_EQ(0x100, tty.input_size);
    ASSERT_EQ(1, tty.output_head);
    ASSERT_EQ(1, tty.output_read);
    ASSERT_EQ(0x100, tty.output_tail);
    /* untouched */
    ASSERT_EQ(0xAAAAAAAA, tty.line_id);
    ASSERT_EQ(0xAAAA, tty.min_chars);
    ASSERT_EQ(0xAA, tty.input_buffer[0]);
}

TEST(tables_and_dfl_call)
{
    int i;
    memset(&tty, 0xAA, sizeof(tty));
    log_reset();
    TTY_$I_INIT(&tty);
    ASSERT_EQ(0, memcmp(tty.func_chars, tty_$i_init_data.func_chars, 18));
    for (i = 0; i < 256; i++) {
        ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[i]);
    }
    ASSERT_STR("dfl;", call_log);
    ASSERT_EQ((unsigned long)&tty, (unsigned long)dfl_tty);
    ASSERT_EQ(0xFF, dfl_use);
    for (i = 0; i < 6; i++) {
        ASSERT_EQ((unsigned long)ARCH_PTR_TO_VA(&tty), (unsigned long)tty.signals[i].tty_desc);
        ASSERT_EQ(tty_$i_init_data.signal_status[i], (uint32_t)tty.signals[i].fault_status);
        ASSERT_EQ(tty_$i_init_data.signal_num[i], tty.signals[i].signal_num);
        ASSERT_EQ(0xAAAA, tty.signals[i].reserved);
    }
}

int main(void)
{
    printf("TTY_$I_INIT tests\n");
    RUN_TEST(image_tables);
    RUN_TEST(scalar_fields);
    RUN_TEST(tables_and_dfl_call);
    TEST_SUMMARY();
}
