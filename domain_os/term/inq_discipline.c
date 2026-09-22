/*
 * TERM_$INQ_DISCIPLINE - Return a line's discipline word
 *
 * Parameters (frame 0x00E1ACAC..0x00E1ACD0):
 *   0x08 line_ptr       - word by reference; its VALUE goes to GET_REAL_LINE
 *   0x0C discipline_ret - receives dtte[line].discipline
 *   0x10 status_ret     - status return (A2)
 *
 * Original address: 0x00e1ac9e, 74 bytes
 *
 *   00e1aca6  lea (0xe2c9f0).l,A5                     ; TERM_$DATA
 *   00e1acb0  subq.l #2 / pea (A2) / move.w (*line) / jsr TERM_$GET_REAL_LINE -> D0
 *   00e1acc2  tst.l (A2) / bne -> exit
 *   00e1acc6  D1 = line*64 - line*8 = line*0x38 (sign-extended word)
 *   00e1acd6  lea (0x0,A5,D1w),A0 / move.w (0x12d4,A0),(A1)   ; 0x12A0 + 0x34
 */

#include "term/term_internal.h"

void TERM_$INQ_DISCIPLINE(short *line_ptr, unsigned short *discipline_ret,
                          status_$t *status_ret)
{
    int16_t line;       /* D0w */

    /* 0x00E1ACB0..0x00E1ACC0 */
    line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);

    /* 0x00E1ACC2 */
    if (*status_ret == status_$ok) {
        /* 0x00E1ACC6..0x00E1ACDA */
        *discipline_ret = (unsigned short)DTTE[line].discipline;
    }
}
