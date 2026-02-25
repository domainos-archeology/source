/*
 * TTY_$I_WORD_ERASE - Erase the previous word from the input buffer
 *
 * Implements word-erase (typically ^W). First skips over any trailing
 * whitespace/separator characters, then deletes back through the
 * previous word (non-separator characters).
 *
 * Character classification uses a bitmap accessed via the A5 register
 * (module global data base). The bitmap uses the formula:
 *   byte_index = (0xFF - ch) >> 3
 *   bit_position = ch & 7
 * A set bit means the character is a "word separator" (whitespace,
 * punctuation, etc.).
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b716
 * Size: 154 bytes
 */

#include "tty/tty_internal.h"

/*
 * tty_$word_sep_bitmap - Word separator character bitmap
 *
 * This bitmap is accessed via A5 in the original code. It classifies
 * characters as word separators for the word-erase function.
 * The bitmap is indexed as: byte[(0xFF - ch) >> 3], bit[ch & 7].
 *
 * TODO(source-qvt): This bitmap is initialized at runtime. The initial values
 * at 0xe2ddb4 are all zeros; the actual separator set is configured
 * during TTY initialization. Need to identify the initialization code.
 *
 * Original address: 0x00e2ddb4 (A5 base)
 */
/* tty_$word_sep_bitmap declared in tty_internal.h */

/*
 * tty_is_word_separator - Check if a character is a word separator
 *
 * Uses the bitmap to determine if a character is considered a
 * word boundary for word-erase purposes.
 */
static int tty_is_word_separator(uint8_t ch)
{
    uint16_t byte_index = (uint16_t)(0xFF - ch) >> 3;
    uint8_t bit_mask = 1 << (ch & 7);
    return (tty_$word_sep_bitmap[byte_index] & bit_mask) != 0;
}

void TTY_$I_WORD_ERASE(tty_desc_t *tty)
{
    int16_t prev;
    uint8_t ch;

    /* Phase 1: Skip trailing word separators (whitespace/punctuation) */
    while (tty->input_tail != tty->input_head) {
        /* Peek at the character before the current tail */
        if (tty->input_tail == 1) {
            prev = 0x100;
        } else {
            prev = tty->input_tail - 1;
        }
        ch = tty->input_buffer[prev];

        if (!tty_is_word_separator(ch)) {
            break;
        }

        TTY_$I_DELETE_CHAR(tty);
    }

    /* Phase 2: Delete back through the word (non-separator characters) */
    while (tty->input_tail != tty->input_head) {
        /* Peek at the character before the current tail */
        if (tty->input_tail == 1) {
            prev = 0x100;
        } else {
            prev = tty->input_tail - 1;
        }
        ch = tty->input_buffer[prev];

        if (tty_is_word_separator(ch)) {
            break;
        }

        TTY_$I_DELETE_CHAR(tty);
    }
}
