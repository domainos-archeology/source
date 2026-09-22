/*
 * TTY_$I_GET_DESC - Get the TTY descriptor for a terminal line
 *
 * 0x00E66738..0x00E667C2 (140 bytes; map "I E66738 OS_TERM", the first
 * routine of that segment).  Frame: line = the word at (0x8,A6), status =
 * the pointer at (0xa,A6): the callers push the line with a bare `move.w`
 * (2 bytes), so the status pointer sits at 0xa rather than 0xc.  The result
 * is returned in A0 from the local (-0x8,A6).
 *
 *   0x00E66744  real = TERM_$GET_REAL_LINE(line, status)
 *   0x00E66756  status != 0 -> exit (the result local is never written)
 *   0x00E6675A  D2w = real * 0x38 (8x - 64x trick), DTTE base 0x00E2DC90
 *   0x00E6676C  DTTE[real].handler_ptr (+0x24) == 0 -> status 0xB000D, exit
 *   0x00E6677A  real == 0 (console):
 *                 DTTY_$USE_DTTY (byte 0x00E2E014) >= 0 and
 *                 DTTE[0].discipline (+0x34) != 2 ->
 *                   TERM_$SET_DISCIPLINE(&line, &word 2 @0x00E667C4, &local)
 *                 SMD_$UNBLANK()   (0x00E6EFB4, always for line 0)
 *   0x00E667AA  result = DTTE[real].handler_ptr
 *
 * The discipline constant is the shared cell term_$const_word_2 (bytes
 * 00 02 at 0x00E667C4), which TERM_$CONTROL case 1 also passes by reference.
 * The earlier emission passed a private zero.
 *
 * Status 0xB000D: "requested line or operation not implemented".
 *
 * Original address: 0x00e66738
 * Size: 140 bytes
 */

#include "tty/tty_internal.h"
#include "term/term.h"
#include "smd/smd.h"
#include "dtty/dtty.h"

tty_desc_t *TTY_$I_GET_DESC(short line, status_$t *status)
{
    short real_line;                          /* D3w */
    status_$t local_status;                   /* (-0x4,A6) */
    /* (-0x8,A6): left unwritten by the image on the two error exits, so the
     * value returned there is whatever the stack held.  NULL stands in. */
    tty_desc_t *result = NULL;

    real_line = TERM_$GET_REAL_LINE(line, status);            /* 0x00E66744 */
    if (*status != status_$ok) {                              /* 0x00E66756 */
        return result;
    }

    if (TERM_$DATA.dtte[real_line].handler_ptr == 0) {        /* 0x00E6676C */
        *status = status_$requested_line_or_operation_not_implemented;
        return result;
    }

    if (real_line == 0) {                                     /* 0x00E6677A */
        if (DTTY_$USE_DTTY >= 0) {                            /* 0x00E6677E tst.b / bmi */
            if (TERM_$DATA.dtte[real_line].discipline != 2) { /* 0x00E66786 */
                TERM_$SET_DISCIPLINE(&line, (void *)&term_$const_word_2,
                                     &local_status);          /* 0x00E6678E */
            }
        }
        SMD_$UNBLANK();                                       /* 0x00E667A4 */
    }

    result = (tty_desc_t *)ARCH_VA_TO_PTR(TERM_$DATA.dtte[real_line].handler_ptr); /* 0x00E667B0 */
    return result;
}
