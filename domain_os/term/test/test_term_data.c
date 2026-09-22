/*
 * term/test/test_term_data.c - the OS_TERM_INIT module block, 0x00E2C9F0
 *
 * TERM_$DATA is the map segment "D E2C9F0 OS_TERM_INIT size = 1398".  Only 59
 * of its bytes are non-zero in the image, in two windows: the handler and
 * descriptor cells at +0x00..+0xC3 and +0x138D plus the keyboard string at
 * +0x1390.  These tests pin the size, the two windows' contents and the three
 * aliases that used to be separate objects (TONE_$CHANNEL and the two
 * PTR_TTY_$I_RCV cells).
 */

#include <stdio.h>
#include <string.h>

#include "term/term_internal.h"

/* term_data.c defines one DXM callback cell; neither the registry nor the
 * handler is under test, so both are stubbed. */
void TERM_$ENQUEUE_TPAD(void **param1) { (void)param1; }

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    return (dxm_$callback_t)(fn != NULL);
}

#include "../term_data.c"


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

#define TERM_BASE 0x00E2C9F0

TEST(the_block_is_exactly_the_map_segment)
{
    ASSERT_EQ(0x1398, sizeof(term_data_t));
    /* TTY_$SPIN_LOCK and TERM_$MAX_DTTE are the segment's interior symbols. */
    ASSERT_EQ(0x00E2DD74 - TERM_BASE,
              __builtin_offsetof(term_data_t, tty_spin_lock));
    ASSERT_EQ(0x00E2DD78 - TERM_BASE,
              __builtin_offsetof(term_data_t, max_dtte));
}

/* Each pointer cell holds the address the SAU2 map gives for its routine. */
TEST(the_handler_cells_hold_their_image_addresses)
{
    ASSERT_EQ(0x00E1B92A, TERM_$DATA.ptr_tty_i_rcv);            /* TTY_$I_RCV */
    ASSERT_EQ(0x00E1B394, TERM_$DATA.ptr_tty_i_drain);
    ASSERT_EQ(0x00E1BECE, TERM_$DATA.ptr_tty_i_hup);
    ASSERT_EQ(0x00E1BEA8, TERM_$DATA.ptr_tty_i_int);
    ASSERT_EQ(0x00E1BE08, TERM_$DATA.ptr_tty_i_err);
    ASSERT_EQ(0x00E1C7A8, TERM_$DATA.ptr_sio_i_tstart);
    ASSERT_EQ(0x00E1C9CE, TERM_$DATA.ptr_sio_i_inhibit_xmit);
    ASSERT_EQ(0x00E1C94A, TERM_$DATA.ptr_sio_i_inhibit_rcv);
    ASSERT_EQ(0x00E67D9C, TERM_$DATA.ptr_sio_i_err);
    ASSERT_EQ(0x00E1D6D0, TERM_$DATA.ptr_dtty_tstart);
    ASSERT_EQ(0x00E1CCC0, TERM_$DATA.ptr_kbd_rcv);              /* KBD_$RCV */
    ASSERT_EQ(0x00E1CE96, TERM_$DATA.ptr_kbd_drain);
    ASSERT_EQ(0x00E1C7A8, TERM_$DATA.ptr_sio_i_tstart_b4);
    ASSERT_EQ(0x00E1B92A, TERM_$DATA.ptr_tty_i_rcv_alt);        /* TTY_$I_RCV */
}

TEST(the_descriptor_words_hold_their_image_values)
{
    ASSERT_EQ(0x0009, TERM_$DATA.w_02);
    ASSERT_EQ(0x000c, TERM_$DATA.w_06);
    ASSERT_EQ(0x000e, TERM_$DATA.w_0c);
    ASSERT_EQ(0x000e, TERM_$DATA.w_0e);
    ASSERT_EQ(0x0003, TERM_$DATA.w_10);
    ASSERT_EQ(0x0001, TERM_$DATA.w_12);
    ASSERT_EQ(0x0009, TERM_$DATA.w_5a);
    ASSERT_EQ(0x0007, TERM_$DATA.w_64);
    ASSERT_EQ(0x0007, TERM_$DATA.w_66);
    ASSERT_EQ(0x0003, TERM_$DATA.w_68);
    ASSERT_EQ(0x0001, TERM_$DATA.w_6a);
    ASSERT_EQ(0x0003, TERM_$DATA.w_6c);
}

/* +0x138D and the five bytes TERM_$SEND_KBD_STRING sends (length 5). */
TEST(the_tail_window_matches_the_image)
{
    static const char image[8] = { (char)0xff, 0x00, (char)0xff, 0x12, 0x21,
                                   0x00, 0x00, 0x00 };
    ASSERT_EQ(0xff, TERM_$DATA.b_138d);
    ASSERT_EQ(5, TERM_$KBD_STRING_LEN);
    ASSERT_EQ(8, sizeof(TERM_$DATA.kbd_string_data));
    ASSERT_EQ(0, memcmp(TERM_$DATA.kbd_string_data, image, sizeof(image)));
}

/* Everything outside the two windows is zero, as in the image. */
TEST(everything_between_the_windows_is_zero)
{
    const unsigned char *p = (const unsigned char *)&TERM_$DATA;
    size_t i;
    int nonzero = 0;
    for (i = 0xC4; i < 0x138A; i++) {
        if (p[i] != 0) nonzero++;
    }
    ASSERT_EQ(0, nonzero);
}

/*
 * The three cells that used to be separate C objects are fields of the block:
 * 0x00E2CA08 = +0x18, 0x00E2CAB0 = +0xC0 and TONE_$CHANNEL = +0x1268.
 */
TEST(the_folded_aliases_land_at_their_image_addresses)
{
    ASSERT_EQ(0x00E2CA08 - TERM_BASE,
              (const char *)&PTR_TTY_$I_RCV_00e2ca08 - (const char *)&TERM_$DATA);
    ASSERT_EQ(0x00E2CAB0 - TERM_BASE,
              (const char *)&PTR_TTY_$I_RCV_00e2cab0 - (const char *)&TERM_$DATA);
    ASSERT_EQ(0x00E2CA78 - TERM_BASE,
              (const char *)&PTR_KBD_$RCV_00e2ca78 - (const char *)&TERM_$DATA);
    ASSERT_EQ(0x00E2DC58 - TERM_BASE,
              (const char *)&TONE_$CHANNEL - (const char *)&TERM_$DATA);
}

int main(void)
{
    printf("=== TERM_$DATA (0x00E2C9F0, OS_TERM_INIT) ===\n");
    RUN_TEST(the_block_is_exactly_the_map_segment);
    RUN_TEST(the_handler_cells_hold_their_image_addresses);
    RUN_TEST(the_descriptor_words_hold_their_image_values);
    RUN_TEST(the_tail_window_matches_the_image);
    RUN_TEST(everything_between_the_windows_is_zero);
    RUN_TEST(the_folded_aliases_land_at_their_image_addresses);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
