/*
 * TERM_$SEND_KBD_STRING - Send a string to the console keyboard line
 *
 * Calls KBD_$PUT for line 0 with type 0 (both arguments point at the same
 * zero word in the code region) and converts the status.  KBD_$PUT's body
 * for a valid line is the empty procedure at 0x00E1CA8A, so in practice
 * only KBD_$GET_DESC's checks and the status conversion take effect.
 *
 * Parameters (frame 0x00E1AB04..0x00E1AB08):
 *   0x08 str    - passed on
 *   0x0C length - passed on
 *
 * Original address: 0x00e1aafc, 42 bytes
 *
 *   00e1ab00  pea (-0x4,A6)                            ; status
 *   00e1ab04  move.l (0xc,A6) / move.l (0x8,A6)
 *   00e1ab0c  pea (0x18,PC)                            ; 0xE1AB0E + 0x18 = 0xE1AB26
 *   00e1ab10  move.l (SP),-(SP)                        ; the same cell again
 *   00e1ab12  jsr KBD_$PUT / lea (0x14,SP),SP
 *   00e1ab1c  pea (-0x4,A6) / bsr 0x00e1aaa8 (TERM_$STATUS_CONVERT)   ; args reclaimed by unlk
 *
 * `gsk read 0xE1AB26 2`: 00 00 - the cell is the word 0, sitting after this
 * routine's rts (0x00E1AB24) and before KBD_$RESET (0x00E1AB28).
 */

#include "term/term_internal.h"

static const uint16_t term_$c_kbd_line_zero = 0;    /* 0x00E1AB26 */

void TERM_$SEND_KBD_STRING(void *str, void *length)
{
    status_$t status;       /* A6-0x4 */

    /* 0x00E1AB00..0x00E1AB18 */
    KBD_$PUT((uint16_t *)&term_$c_kbd_line_zero, (uint16_t *)&term_$c_kbd_line_zero,
             str, (uint16_t *)length, &status);

    /* 0x00E1AB1C..0x00E1AB20 */
    TERM_$STATUS_CONVERT(&status);
}
