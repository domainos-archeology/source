/*
 * PROC2_$SHUTDOWN - Suspend every other bound process
 *
 * Re-emitted from the image (0x00E415C2..0x00E4161E, 94 bytes).
 *
 *   00e415d0  move.w (0x1e0,A5),D0w       ; P2_INFO_ALLOC_PTR
 *   00e415d4  movea.l #0xe2060a,A0 ; A2   ; &PROC1_$AS_ID (movea sets no flags)
 *   00e415dc  beq.b exit                  ; hence the beq tests the move.w
 *   00e415f0  move.w (-0x4e,A3),D1w       ; entry+0x96 asid
 *   00e415f4  cmp.w (A2),D1w / beq skip   ; == PROC1_$AS_ID
 *   00e415fc  btst.l #0x8,D1 / beq skip   ; flags & 0x0100 (BOUND)
 *   00e41602  pea (-0x2c,A6) ; pea (-0xe4,A3) ; PROC2_$SUSPEND(&entry->uid, &status)
 *   00e41610  move.w (-0xd2,A3),D0w / bne ; entry+0x12
 *
 * No lock is taken; the status is a never-read local.  Sole caller
 * 0x00E6D4F6.
 *
 * Original address: 0x00e415c2
 */

#include "proc2/proc2_internal.h"

void PROC2_$SHUTDOWN(void)
{
    int16_t index;               /* D0 */
    proc2_info_t *entry;         /* A3 (biased) */
    status_$t status;            /* A6-0x2C */

    /* 0x00E415D0-0x00E415DC */
    index = (int16_t)P2_INFO_ALLOC_PTR;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);                        /* 0x00E415E0-0x00E415EC */
        /* 0x00E415F0-0x00E41600 */
        if (entry->asid != PROC1_$AS_ID && (entry->flags & PROC2_FLAG_BOUND) != 0) {
            PROC2_$SUSPEND(&entry->uid, &status);            /* 0x00E4160A */
        }
        index = (int16_t)entry->next_index;                  /* 0x00E41610 */
    }
}
