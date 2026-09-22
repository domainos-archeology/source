/*
 * TERM_$GET_REAL_LINE - Map a logical terminal line to a real one
 *
 * Line 0 is the display console (DTTY_$CTRL); line 1 is also the console
 * when the caller is process 1, otherwise real line 1; other values pass
 * through.  The result must then be at most 3 and below TERM_$MAX_DTTE.
 *
 * Parameters (frame 0x00E1A9D8..0x00E1A9DC):
 *   0x08 line_num   - word by VALUE (D0w)
 *   0x0A status_ret - status return (A0)
 *
 * Returns (D0w): the real line; on an error it is the mapped value that
 * failed the check.
 *
 * Original address: 0x00e1a9d4, 82 bytes
 *
 *   00e1a9e0  clr.l (A0)
 *   00e1a9e2  moveq #1,D1 / cmp.w D1w,D0w / bne -> not 1
 *   00e1a9e8  cmp.w (0x00e20608).l,D1w / bne -> D0 = 1        ; PROC1_$CURRENT == 1?
 *   00e1a9f0  move.w (0x00e2e00e).l,D0w / bra check           ; DTTY_$CTRL
 *   00e1a9fc  tst.w D0w / bne -> check
 *   00e1aa00  move.w (0x00e2e00e).l,D0w                       ; line 0 -> DTTY_$CTRL
 *   00e1aa06  cmpi.w #3,D0w / bls; 0xb0007                    ; UNSIGNED
 *   00e1aa14  cmp.w (0x00e2dd78).l,D0w / bcs; 0xb000d         ; UNSIGNED, TERM_$MAX_DTTE
 */

#include "term/term_internal.h"

short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret)
{
    uint16_t line;          /* D0w */

    /* 0x00E1A9E0 */
    *status_ret = status_$ok;

    /* 0x00E1A9E2..0x00E1AA00 */
    line = (uint16_t)line_num;
    if (line == 1) {
        if (PROC1_$CURRENT == 1) {
            line = DTTY_$CTRL;
        } else {
            line = 1;
        }
    } else if (line == 0) {
        line = DTTY_$CTRL;
    }

    /* 0x00E1AA06..0x00E1AA1C */
    if (line > 3) {
        *status_ret = status_$invalid_line_number;
    } else if (line >= (uint16_t)TERM_$MAX_DTTE) {
        *status_ret = status_$requested_line_or_operation_not_implemented;
    }

    return (short)line;
}
