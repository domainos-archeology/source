/*
 * win/error_que.c - WIN_$ERROR_QUE (0x00E19D54, 14 bytes)
 *
 * Jump-table entry +0x14, called by DISK_$ERROR_QUE as
 * error_que(vol, is_timeout, result).  The Winchester driver has no error
 * queue: it clears the result byte at (0xe,A6) - argument 3, after the
 * 4-byte vol and the 2-byte is_timeout - and returns.  The slot is
 * declared as a Pascal function returning a word in the jump table, but
 * this routine never loads D0 (`link.w A6,0x0` / `movea.l` / `clr.b` /
 * `unlk` / `rts`), so in C it is a procedure.
 */

#include "win/win_internal.h"

void WIN_$ERROR_QUE(void *vol, uint16_t is_timeout, int8_t *result)
{
    (void)vol;
    (void)is_timeout;

    /* 0x00E19D58-0x00E19D5C */
    *result = 0;
}
