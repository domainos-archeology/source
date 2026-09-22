/* tty/test/test_i_set_dfl_funcs.c - tty_$i_set_funcs (0x00E6720E) and
 * TTY_$I_SET_DFL_FUNCS (0x00E6726E), with the real tty_data.c tables. */
#include "tty/tty_internal.h"
#include "test_harness.h"

void TTY_$I_DXM_SIGNAL(tty_signal_entry_t **e) { (void)e; }
dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn) { return (dxm_$callback_t)(fn != NULL); }

#include "../tty_data.c"
#include "../i_set_dfl_funcs.c"

static tty_desc_t tty;

static void reset(void)
{
    int i;
    memset(&tty, 0, sizeof(tty));
    for (i = 0; i < TTY_MAX_FUNC_CHARS; i++) {
        tty.func_chars[i] = (uint8_t)(0x80 + i);      /* distinct slots */
    }
    for (i = 0; i < 256; i++) {
        tty.char_class[i] = 0x77;                     /* sentinel */
    }
}

TEST(mask_selects_slots)
{
    reset();
    tty.func_enabled = 0xFFFFFFFF;
    tty_$i_set_funcs(&tty, 0x5, (char)0xFF);
    ASSERT_EQ(tty_$i_dfl_func_classes[0], tty.char_class[0x80]);
    ASSERT_EQ(0x77, tty.char_class[0x81]);
    ASSERT_EQ(tty_$i_dfl_func_classes[2], tty.char_class[0x82]);
    ASSERT_EQ(0x77, tty.char_class[0x83]);
}

TEST(disabled_or_not_dfl_gives_normal)
{
    reset();
    tty.func_enabled = 0x2;
    tty_$i_set_funcs(&tty, 0x3, (char)0xFF);
    ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[0x80]);      /* not enabled */
    ASSERT_EQ(tty_$i_dfl_func_classes[1], tty.char_class[0x81]);

    reset();
    tty.func_enabled = 0xFFFFFFFF;
    tty_$i_set_funcs(&tty, 0x3, 0x7F);                           /* use_dfl false */
    ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[0x80]);
    ASSERT_EQ(TTY_CHAR_CLASS_NORMAL, tty.char_class[0x81]);
}

TEST(only_18_slots)
{
    reset();
    tty.func_enabled = 0xFFFFFFFF;
    tty_$i_set_funcs(&tty, 0xFFFFFFFF, (char)0xFF);
    ASSERT_EQ(tty_$i_dfl_func_classes[17], tty.char_class[0x91]);
    ASSERT_EQ(0x77, tty.char_class[0x92]);
}

TEST(dfl_wrapper_uses_enable_mask)
{
    int i;
    reset();
    tty.func_enabled = 0xFFFFFFFF;
    TTY_$I_SET_DFL_FUNCS(&tty, (char)0xFF);
    ASSERT_EQ(0x0001FFFF, tty_$i_dfl_func_enable_mask);
    for (i = 0; i < 17; i++) {
        ASSERT_EQ(tty_$i_dfl_func_classes[i], tty.char_class[0x80 + i]);
    }
    ASSERT_EQ(0x77, tty.char_class[0x91]);                        /* slot 17 not in mask */
}

int main(void)
{
    printf("tty_$i_set_funcs tests\n");
    RUN_TEST(mask_selects_slots);
    RUN_TEST(disabled_or_not_dfl_gives_normal);
    RUN_TEST(only_18_slots);
    RUN_TEST(dfl_wrapper_uses_enable_mask);
    TEST_SUMMARY();
}
