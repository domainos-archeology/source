/*
 * PROC2_$UPID_TO_UID - Look a process's UID up by Unix pid
 *
 * Re-emitted from the image (0x00E40ECE..0x00E40F6A, 158 bytes).
 *
 * Frame (link.w A6,-0x14; A5 = 0xE7BE84): (0x8,A6) upid ptr -> D2 = *ptr,
 * (0xC,A6) uid_ret <- A6-0x8, (0x10,A6) status_ret <- A6-0xC (cleared at
 * 0x00E40EE2).  The allocated list is walked comparing entry+0x16; on a
 * match the entry's UID is copied to A6-0x8 and a zombie (btst #13) gives
 * status_$proc2_zombie.  A6-0x8 is written ONLY on a match, so *uid_ret
 * is indeterminate on the not-found path (reproduced: no initialiser).
 *
 * References: 0x00E5CE86 and the SVC table entry at 0x00E7B732.
 *
 * Original address: 0x00e40ece
 */

#include "proc2/proc2_internal.h"

void PROC2_$UPID_TO_UID(int16_t *upid, uid_t *uid_ret, status_$t *status_ret)
{
    int16_t search_upid;         /* D2 */
    int16_t index;               /* D0 */
    proc2_info_t *entry;         /* A0 (biased) */
    status_$t status;            /* A6-0xC */
    uid_t found_uid;             /* A6-0x8: only assigned on a match */

    /* 0x00E40EDC-0x00E40EE2 */
    search_upid = *upid;
    status = status_$ok;

    /* 0x00E40EE6-0x00E40EF2 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E40EF4: D0 = alloc ptr; beq not-found */
    index = (int16_t)P2_INFO_ALLOC_PTR;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);                        /* 0x00E40EFC-0x00E40F08 */
        if (entry->upid == (uint16_t)search_upid) {          /* 0x00E40F0C */
            found_uid.high = entry->uid.high;                /* 0x00E40F12-0x00E40F1A */
            found_uid.low = entry->uid.low;
            if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {   /* 0x00E40F1E-0x00E40F26 */
                status = status_$proc2_zombie;               /* 0x00E40F28 */
            }
            goto done;                                       /* 0x00E40F30 */
        }
        index = (int16_t)entry->next_index;                  /* 0x00E40F32 */
    }
    status = status_$proc2_uid_not_found;                    /* 0x00E40F38 */

done:
    /* 0x00E40F40-0x00E40F5E */
    ML_$UNLOCK(PROC2_LOCK_ID);
    uid_ret->high = found_uid.high;
    uid_ret->low = found_uid.low;
    *status_ret = status;
}
