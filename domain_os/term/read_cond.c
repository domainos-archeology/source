/*
 * TERM_$READ_COND - Conditional (non-blocking) read from a terminal line
 *
 * Calls TTY_$K_GET with the shared "conditional" option word and converts
 * the status.  Unlike TERM_$READ the line is passed through unmapped.
 *
 * Parameters (frame 0x00E668A2..0x00E668B4):
 *   0x08 line_ptr   - passed on to TTY_$K_GET
 *   0x0C buffer     - passed on
 *   0x10 count      - passed on
 *   0x14 status_ret - status return (A2)
 *
 * Returns (D0w): TTY_$K_GET's result.
 *
 * Original address: 0x00e6689a, 62 bytes
 *
 *   00e668a6  pea (A2) / move.l (0x10,A6) / move.l (0xc,A6) /
 *             pea (-0x1c,PC) -> 0xE66896 (word 1) / move.l (0x8,A6)
 *   00e668b8  jsr TTY_$K_GET / lea (0x14,SP),SP -> D2w
 *   00e668c4  pea (A2) / jsr 0x00e1aaa8 (TERM_$STATUS_CONVERT)   ; args reclaimed by unlk
 *   00e668cc  move.w D2w,D0w
 */

#include "term/term_internal.h"

unsigned short TERM_$READ_COND(void *line_ptr, void *buffer, void *count,
                               status_$t *status_ret)
{
    uint16_t result;        /* D2w */

    /* 0x00E668A6..0x00E668C2 */
    result = TTY_$K_GET((short *)line_ptr, (void *)&term_$const_word_1, buffer,
                        (ushort *)count, status_ret);

    /* 0x00E668C4..0x00E668C6 */
    TERM_$STATUS_CONVERT(status_ret);

    /* 0x00E668CC */
    return result;
}
