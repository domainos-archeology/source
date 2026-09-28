/*
 * DI_$INIT_Q_ELEM - Zero a deferred-interrupt queue element
 *
 * Re-emitted from the image (0x00E209D6..0x00E209E4, 16 bytes); assembler
 * with the ordinary stack convention, as DI_$ENQ:
 *
 *   00e209d6  movea.l (0x4,SP),A0    ; elem (arg 1)
 *   00e209da  move.w #0x3,D0w
 *   00e209de  clr.l (A0)+            ; dbf: 4 longwords = the 16-byte element
 *   00e209e0  dbf D0w,0x00e209de
 *
 * Callers: 0x00E2FEC4 and 0x00E2FED2 (TIME_$INIT).
 *
 * Original address: 0x00e209d6
 */

#include "di/di_internal.h"

void DI_$INIT_Q_ELEM(di_queue_elem_t *elem)
{
    uint32_t *p = (uint32_t *)elem;
    int i;

    /* 0x00E209DA-0x00E209E0: moveq-style #3 + dbf = 4 iterations */
    for (i = 0; i < 4; i++) {
        *p++ = 0;
    }
}
