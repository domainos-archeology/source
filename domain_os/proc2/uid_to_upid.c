/*
 * PROC2_$UID_TO_UPID - Look a process's Unix pid up by UID
 *
 * Re-emitted from the image (0x00E40F6C..0x00E4100A, 160 bytes).
 *
 * Frame (link.w A6,-0x14; A5 = 0xE7BE84): (0x8,A6) proc_uid copied to
 * A6-0x8, (0xC,A6) upid_ret <- D2, (0x10,A6) status_ret <- A6-0xC
 * (cleared at 0x00E40F86).
 *
 * The allocated list is walked comparing entry+0x00 with cmpm.l (two
 * longwords).  On a match D2 = entry+0x16 and a zombie (btst #13) yields
 * status_$proc2_zombie; on no match status_$proc2_uid_not_found.  D2 is
 * written ONLY on a match, so *upid_ret is indeterminate on the not-found
 * path (reproduced: no initialiser).
 *
 * Only reference: the SVC table entry at 0x00E7B8AA.
 *
 * Original address: 0x00e40f6c
 */

#include "proc2/proc2_internal.h"

void PROC2_$UID_TO_UPID(uid_t *proc_uid, uint16_t *upid_ret, status_$t *status_ret)
{
    uid_t uid;                   /* A6-0x8 */
    status_$t status;            /* A6-0xC */
    int16_t index;               /* D0 */
    proc2_info_t *entry;         /* A0 (biased) */
    uint16_t upid;               /* D2: only assigned on a match */

    /* 0x00E40F7A-0x00E40F86 */
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;
    status = status_$ok;

    /* 0x00E40F8A-0x00E40F96 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E40F98: D0 = alloc ptr; beq not-found */
    index = (int16_t)PROC2_$UNWIRED_DATA.info_alloc_ptr;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);                        /* 0x00E40FA0-0x00E40FB0 */
        /* 0x00E40FBA-0x00E40FC0: cmpm.l twice */
        if (entry->uid.high == uid.high && entry->uid.low == uid.low) {
            upid = entry->upid;                              /* 0x00E40FC2 */
            if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {   /* 0x00E40FC6-0x00E40FCE */
                status = status_$proc2_zombie;               /* 0x00E40FD0 */
            }
            goto done;                                       /* 0x00E40FD8 */
        }
        index = (int16_t)entry->next_index;                  /* 0x00E40FDA */
    }
    status = status_$proc2_uid_not_found;                    /* 0x00E40FE0 */

done:
    /* 0x00E40FE8-0x00E40FFE */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *upid_ret = upid;
    *status_ret = status;
}
