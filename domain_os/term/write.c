/*
 * TERM_$WRITE - Write to a terminal line
 *
 * Copies the caller's count word into the frame, calls TTY_$K_PUT with the
 * shared "blocking" option word and converts the status.  The line is
 * passed through unmapped.
 *
 * Parameters (frame 0x00E668DE..0x00E668F8):
 *   0x08 line_ptr   - passed on to TTY_$K_PUT
 *   0x0C buffer     - passed on
 *   0x10 count_ptr  - word by reference; its VALUE is copied to A6-0x2 and
 *                     the copy's address is what TTY_$K_PUT receives
 *   0x14 status_ret - status return (A2)
 *
 * Original address: 0x00e668d8, 62 bytes
 *
 *   00e668e2  movea.l (0x10,A6),A0 / move.w (A0),(-0x2,A6)
 *   00e668ea  pea (A2) / pea (-0x2,A6) / move.l (0xc,A6)
 *   00e668f4  pea (-0x5e,PC)                    ; 0xE668F6 - 0x5E = 0xE66898, the word 0
 *   00e668f8  move.l (0x8,A6) / jsr TTY_$K_PUT / lea (0x14,SP),SP
 *   00e66906  pea (A2) / jsr 0x00e1aaa8 (TERM_$STATUS_CONVERT)   ; args reclaimed by unlk
 *
 * The option word is the same cell TERM_$READ and TERM_$INQUIRE use
 * (term_$const_word_0, defined in term/read.c).
 */

#include "term/term_internal.h"

void TERM_$WRITE(void *line_ptr, void *buffer, unsigned short *count_ptr,
                 status_$t *status_ret)
{
    ushort count;       /* A6-0x2 */

    /* 0x00E668E2..0x00E668E6 */
    count = *count_ptr;

    /* 0x00E668EA..0x00E66902 */
    TTY_$K_PUT((short *)line_ptr, (void *)&term_$const_word_0, buffer, &count,
               status_ret);

    /* 0x00E66906..0x00E66908 */
    TERM_$STATUS_CONVERT(status_ret);
}
