/* tty/test/test_i_word_erase.c - TTY_$I_WORD_ERASE (0x00E1B716) with the
 * real tty_data.c bitmap. */
#include "tty/tty_internal.h"
#include "test_harness.h"

void TTY_$I_DXM_SIGNAL(tty_signal_entry_t **e) { (void)e; }
dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn) { return (dxm_$callback_t)(fn != NULL); }

static tty_desc_t tty;
void TTY_$I_DELETE_CHAR(tty_desc_t *t)
{
    (void)t;
    logf_call("del;");
    tty.input_tail = (tty.input_tail == 1) ? 0x100 : (uint16_t)(tty.input_tail - 1);
}

#include "../tty_data.c"
#include "../i_word_erase.c"

static void load(uint16_t start, const char *s)
{
    uint16_t pos = start;
    memset(&tty, 0, sizeof(tty));
    tty.input_read = start; tty.input_head = start;
    while (*s) {
        tty.input_buffer[pos - 1] = (uint8_t)*s++;
        pos = (pos == 0x100) ? 1 : (uint16_t)(pos + 1);
    }
    tty.input_tail = pos;
    log_reset();
}

TEST(bitmap_bytes)
{
    ASSERT_EQ(0x01, tty_$word_sep_bitmap[0x1b]);
    ASSERT_EQ(0x37, tty_$word_sep_bitmap[0x1e]);
    ASSERT_EQ(1, tty_is_word_separator(' '));
    ASSERT_EQ(1, tty_is_word_separator('\t'));
    ASSERT_EQ(1, tty_is_word_separator('\r'));
    ASSERT_EQ(0, tty_is_word_separator('\v'));
    ASSERT_EQ(0, tty_is_word_separator('a'));
    ASSERT_EQ(0, tty_is_word_separator(0x7f));
}

TEST(erases_trailing_spaces_then_word)
{
    load(1, "ab cd  ");
    TTY_$I_WORD_ERASE(&tty);
    ASSERT_STR("del;del;del;del;", call_log);
    ASSERT_EQ(4, tty.input_tail);          /* "ab " left */
}

TEST(stops_at_head)
{
    load(5, "  ");
    TTY_$I_WORD_ERASE(&tty);
    ASSERT_STR("del;del;", call_log);
    ASSERT_EQ(5, tty.input_tail);

    load(5, "word");
    TTY_$I_WORD_ERASE(&tty);
    ASSERT_STR("del;del;del;del;", call_log);

    load(5, "");
    TTY_$I_WORD_ERASE(&tty);
    ASSERT_STR("", call_log);
}

TEST(wraps_around_ring)
{
    load(0xFE, "x yz");                    /* 0xFE 0xFF 0x100 1, tail 2 */
    ASSERT_EQ(2, tty.input_tail);
    TTY_$I_WORD_ERASE(&tty);
    ASSERT_STR("del;del;", call_log);
    ASSERT_EQ(0x100, tty.input_tail);
}

int main(void)
{
    printf("TTY_$I_WORD_ERASE tests\n");
    RUN_TEST(bitmap_bytes);
    RUN_TEST(erases_trailing_spaces_then_word);
    RUN_TEST(stops_at_head);
    RUN_TEST(wraps_around_ring);
    TEST_SUMMARY();
}
