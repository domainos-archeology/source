/*
 * DI_$INIT_Q_ELEM - Initialize a deferred interrupt queue element
 *
 * Clears all 16 bytes of a DI queue element structure, preparing
 * it for use in the deferred interrupt queue.
 *
 * From: 0x00e209d6
 *
 * Original assembly:
 *   00e209d6    movea.l (0x4,SP),A0    ; Load elem pointer
 *   00e209da    move.w #0x3,D0w        ; Loop counter = 3 (4 iterations: 3,2,1,0)
 *   00e209de    clr.l (A0)+            ; Clear longword, advance pointer
 *   00e209e0    dbf D0w,0x00e209de     ; Decrement and branch if >= 0
 *   00e209e4    rts
 */

#include "di/di.h"

void DI_$INIT_Q_ELEM(di_queue_elem_t *elem)
{
    uint32_t *p = (uint32_t *)elem;
    int i;

    /*
     * Clear 4 longwords (16 bytes total).
     * The dbf loop runs for counter values 3, 2, 1, 0 (4 iterations).
     */
    for (i = 0; i < 4; i++) {
        *p++ = 0;
    }
}
