/*
 * TERM_$SET_DISCIPLINE - Set a logical line's discipline
 *
 * Maps the line with TERM_$GET_REAL_LINE and, if that succeeded, hands the
 * real line (by reference, from a frame word) and the caller's discipline
 * pointer to TERM_$SET_REAL_LINE_DISCIPLINE.
 *
 * Parameters (frame 0x00E1AB30..0x00E1AB50):
 *   0x08 line_ptr   - word by reference; its VALUE goes to GET_REAL_LINE
 *   0x0C discipline - passed on unchanged
 *   0x10 status_ret - status return (A2)
 *
 * Original address: 0x00e1ab2a, 56 bytes
 *
 *   00e1ab34  subq.l #2 / pea (A2) / move.w (*line) / jsr TERM_$GET_REAL_LINE
 *   00e1ab46  move.w D0w,(-0x2,A6)
 *   00e1ab4a  tst.l (A2) / bne -> exit
 *   00e1ab4e  pea (A2) / move.l (0xc,A6) / pea (-0x2,A6) / bsr TERM_$SET_REAL_LINE_DISCIPLINE
 *             ; args reclaimed by unlk
 */

#include "term/term_internal.h"

void TERM_$SET_DISCIPLINE(short *line_ptr, void *discipline, status_$t *status_ret)
{
    uint16_t real_line;     /* A6-0x2 */

    /* 0x00E1AB34..0x00E1AB46 */
    real_line = (uint16_t)TERM_$GET_REAL_LINE(*line_ptr, status_ret);

    /* 0x00E1AB4A */
    if (*status_ret == status_$ok) {
        /* 0x00E1AB4E..0x00E1AB58 */
        TERM_$SET_REAL_LINE_DISCIPLINE(&real_line, (short *)discipline, status_ret);
    }
}
