/*
 * PROC2_$SIGBLOCK - Add signals to the calling process's blocked set
 *
 * Re-emitted from the image (0x00E3F63E..0x00E3F6BE, 130 bytes) and
 * verified; the previous body was faithful.
 *
 * Frame (link.w A6,-0x10; A5 = 0xE7BE84):
 *   (0x8,A6)  mask ptr -> D3 = *ptr    (0xC,A6)  result -> A2 (two longwords)
 *   The caller's entry (A3, biased, muls) is found BEFORE the lock.
 *
 *   00e3f684  move.l (-0x6c,A3),D2        ; D2 = old entry+0x78 (the result)
 *   00e3f688  or.l D3,(-0x6c,A3)          ; entry+0x78 |= mask
 *   00e3f69a  move.l (-0x6c,A3),(A2)      ; result[0] = new +0x78 (after unlock)
 *   00e3f6a2  btst.l #0xa,D1              ; flags & 0x0400 -> result[1] = 1 : 0
 *   00e3f6b4  move.l D2,D0                ; return the old mask
 *
 * Only reference: the SVC table entry at 0x00E7B5EA.
 *
 * Original address: 0x00e3f63e
 */

#include "proc2/proc2_internal.h"

uint32_t PROC2_$SIGBLOCK(uint32_t *mask_ptr, uint32_t *result)
{
    proc2_info_t *entry;         /* A3 */
    uint32_t old_mask;           /* D2 */
    uint32_t new_bits;           /* D3 */

    /* 0x00E3F64C-0x00E3F674 */
    new_bits = *mask_ptr;
    entry = P2_INFO_ENTRY((int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT));

    /* 0x00E3F664/0x00E3F678-0x00E3F682 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F684-0x00E3F688 */
    old_mask = entry->sig_blocked_2;
    entry->sig_blocked_2 |= new_bits;

    /* 0x00E3F68C-0x00E3F698 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E3F69A-0x00E3F6B0: read back outside the lock */
    result[0] = entry->sig_blocked_2;
    result[1] = ((entry->flags & 0x0400) != 0) ? 1u : 0u;

    return old_mask;                                         /* 0x00E3F6B4 */
}
