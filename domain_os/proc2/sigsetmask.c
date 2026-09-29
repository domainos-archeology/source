/*
 * PROC2_$SIGSETMASK - Replace the calling process's blocked set
 *
 * Re-emitted from the image (0x00E3F6C0..0x00E3F758, 154 bytes) and
 * verified; the previous body was faithful.
 *
 * Frame (link.w A6,-0x10; A5 = 0xE7BE84): (0x8,A6) mask ptr -> D3,
 * (0xC,A6) result -> A2.  The caller's entry (A3) is found before the lock.
 *
 *   00e3f706  D2 = entry+0x78 (returned in D0)
 *   00e3f70a  entry+0x78 = *mask
 *   00e3f70e  if (+0x80 & ~+0x78) != 0: DELIVER_PENDING(+0x1C)   (under the lock)
 *   00e3f734  result[0] = +0x78; result[1] = flags & 0x0400 ? 1 : 0 (after unlock)
 *
 * Only reference: the SVC table entry at 0x00E7B5EE.
 *
 * Original address: 0x00e3f6c0
 */

#include "proc2/proc2_internal.h"

uint32_t PROC2_$SIGSETMASK(uint32_t *mask_ptr, uint32_t *result)
{
    proc2_info_t *entry;         /* A3 */
    uint32_t old_mask;           /* D2 */
    uint32_t new_mask;           /* D3 */

    /* 0x00E3F6CE-0x00E3F6F6 */
    new_mask = *mask_ptr;
    entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E3F6E6/0x00E3F6FA-0x00E3F704 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F706-0x00E3F70A */
    old_mask = entry->sig_blocked_2;
    entry->sig_blocked_2 = new_mask;

    /* 0x00E3F70E-0x00E3F724 */
    if ((entry->sig_mask_2 & ~entry->sig_blocked_2) != 0) {
        PROC2_$DELIVER_PENDING_INTERNAL((int16_t)entry->self_index);
    }

    /* 0x00E3F726-0x00E3F732 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E3F734-0x00E3F74A */
    result[0] = entry->sig_blocked_2;
    result[1] = ((entry->flags & 0x0400) != 0) ? 1u : 0u;

    return old_mask;                                         /* 0x00E3F74E */
}
