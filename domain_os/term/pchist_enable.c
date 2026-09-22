/*
 * TERM_$PCHIST_ENABLE - Set the process-history enable word
 *
 * Parameters (frame 0x00E72456..0x00E7245C):
 *   0x08 enable_ptr - word by reference
 *   0x0C status_ret - cleared
 *
 * Original address: 0x00e72450, 34 bytes
 *
 *   00e72456  movea.l (0xc,A6),A0 / clr.l (A0)
 *   00e7245c  movea.l (0x8,A6),A1 / movea.l #0xe2c9f0,A2
 *   00e72466  move.w (A1),(0x1294,A2)              ; TERM_$DATA.pchist_enable
 */

#include "term/term_internal.h"

void TERM_$PCHIST_ENABLE(unsigned short *enable_ptr, status_$t *status_ret)
{
    /* 0x00E72456..0x00E7245A */
    *status_ret = status_$ok;

    /* 0x00E7245C..0x00E72466 */
    TERM_$DATA.pchist_enable = *enable_ptr;
}
