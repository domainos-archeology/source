/*
 * tty/test/test_tty_data.c - the TTY module block, 0x00E8242C..0x00E82458
 *
 * The map segment "D E8242C TTY size = 2C" exports no interior symbol.  The
 * three objects tty/tty_data.c defines have to fill it exactly, and the class
 * table has to line up index-for-index with the default function characters
 * TTY_$I_INIT copies from 0x00E351D8.
 */

#include "tty/tty_internal.h"

/*
 * tty_data.c defines one DXM callback cell.  On the host that cell is filled
 * in by a constructor calling dxm_$callback_cell(); neither the registry nor
 * the handler is under test here, so both are stubbed.
 */
void TTY_$I_DXM_SIGNAL(tty_signal_entry_t **entry) { (void)entry; }

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    return (dxm_$callback_t)(fn != NULL);
}

#include "../tty_data.c"

#include <stdio.h>

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-46s %s\n", #name, current_failed ? "FAIL" : "ok");          \
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

/* 0x00E8242C + 0x24 + 0x04 + 0x04 = 0x00E82458, the end of the segment. */
TEST(the_three_objects_fill_the_2c_byte_segment)
{
    ASSERT_EQ(0x24, sizeof(tty_$i_dfl_func_classes));
    ASSERT_EQ(0x2C, (int)(sizeof(tty_$i_dfl_func_classes)
                          + sizeof(tty_$i_dfl_func_enable_mask)
                          + sizeof(DAT_00e82454)));
}

/* One class word per function slot; the descriptor's func_chars is the same
 * length (tty_desc_t + 0x24, TTY_MAX_FUNC_CHARS entries). */
TEST(class_table_has_one_entry_per_function_char)
{
    ASSERT_EQ(TTY_MAX_FUNC_CHARS,
              (int)(sizeof(tty_$i_dfl_func_classes)
                    / sizeof(tty_$i_dfl_func_classes[0])));
    ASSERT_EQ(0x12, TTY_MAX_FUNC_CHARS);
}

/*
 * The two classes bead source-2qng pinned: default function 13 is ^S (XOFF)
 * and function 14 is ^Q (XON), which is what makes class 5 stop output and
 * class 6 restart it.
 */
TEST(xoff_and_xon_land_on_classes_5_and_6)
{
    ASSERT_EQ(0x05, tty_$i_dfl_func_classes[13]);
    ASSERT_EQ(0x06, tty_$i_dfl_func_classes[14]);
    ASSERT_EQ(0x07, tty_$i_dfl_func_classes[0]);
    ASSERT_EQ(0x0000, tty_$i_dfl_func_classes[17]);
}

/* Image longwords at 0x00E82450 and 0x00E82454. */
TEST(the_two_masks_keep_their_image_values)
{
    ASSERT_EQ(0x0001FFFFu, tty_$i_dfl_func_enable_mask);
    ASSERT_EQ(0x000000D0u, DAT_00e82454);
}

int main(void)
{
    printf("=== tty module block (0x00E8242C) ===\n");
    RUN_TEST(the_three_objects_fill_the_2c_byte_segment);
    RUN_TEST(class_table_has_one_entry_per_function_char);
    RUN_TEST(xoff_and_xon_land_on_classes_5_and_6);
    RUN_TEST(the_two_masks_keep_their_image_values);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
