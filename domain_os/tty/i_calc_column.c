/*
 * TTY_$I_CALC_COLUMN - Calculate the display column position
 *
 * Walks the input buffer from position 'start' to the current tail,
 * tracking the display column. Handles TAB (expands to next 8-col
 * boundary), BS (decrements column), CR (resets to 0), control chars
 * (add 2 columns if echo_ctlecho is set), and normal chars (add 1).
 *
 * This is used by TTY_$I_DELETE_CHAR to figure out how many columns
 * to erase when deleting a TAB character.
 *
 * Parameters:
 *   buf        - Pointer to the circular buffer read position
 *                (i.e., &tty->input_read; data starts at buf+5)
 *   start      - Starting position index in the buffer (1-256)
 *   column     - Initial column value
 *   echo_flags - Echo flags (bit 4 = echo control chars as ^X)
 *
 * Returns:
 *   The calculated column position after processing all characters
 *
 * 0x00E1B4B6..0x00E1B536 (130 bytes), verified against the image:
 *   0x00E1B4BE  A0 = buf (0x8), D1w = start (0xc), D2w = column (0xe),
 *               D0 = echo_flags (0x10, longword); D3w = (0x2,A0) = tail
 *   0x00E1B4D2  bra to the test: the loop runs while pos != tail
 *   0x00E1B4D4  ch = (0x5,A0,pos)  = data[pos - 1]
 *   0x00E1B4D8  TAB:  column = ((column & 0xffff) + 7) >> 3 << 3 (32-bit,
 *               result truncated to the word D2w)
 *   0x00E1B4EC  BS:   if column != 0, column - 1
 *   0x00E1B4FA  CR:   column = 0
 *   0x00E1B504  ch < 0x20 (bcs, unsigned) or ch == 0x7f: +2 only when
 *               echo_flags bit 4 is set (btst.l #4,D0)
 *   0x00E1B51A  else +1
 *   0x00E1B51C  pos == 0x100 ? 1 : pos + 1
 *   0x00E1B52C  result is the column word in D0w
 *
 * Original address: 0x00e1b4b6
 * Size: 130 bytes
 */

#include "tty/tty_internal.h"

uint16_t TTY_$I_CALC_COLUMN(void *buf, int16_t start, uint16_t column, uint32_t echo_flags)
{
    int16_t pos = start;
    int16_t tail = *(int16_t *)((char *)buf + 2);
    uint8_t ch;

    while (pos != tail) {
        ch = *((uint8_t *)buf + 5 + pos);

        if (ch == 0x09) {
            /* TAB: advance to next 8-column boundary */
            column = (column + 7) & 0xFFF8;
        } else if (ch == 0x08) {
            /* BS: back up one column if not at zero */
            if (column != 0) {
                column = column - 1;
            }
        } else if (ch == 0x0D) {
            /* CR: reset to column 0 */
            column = 0;
        } else if (ch < 0x20 || ch == 0x7F) {
            /* Control character: if echo_ctlecho set, takes 2 columns (^X) */
            if ((echo_flags & 0x10) != 0) {
                column = column + 2;
            }
        } else {
            /* Normal printable character: 1 column */
            column = column + 1;
        }

        /* Advance position in circular buffer (1..256, wraps from 256 to 1) */
        if (pos == 0x100) {
            pos = 1;
        } else {
            pos = pos + 1;
        }
    }

    return column;
}
