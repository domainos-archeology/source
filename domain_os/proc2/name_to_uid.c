/*
 * PROC2_$NAME_TO_UID - Look a process up by name
 *
 * Re-emitted from the image (0x00E3EAD0..0x00E3EB8A, 188 bytes).
 *
 * Frame (link.w A6,-0xC; A5 = 0xE7BE84):
 *   (0x8,A6)  name        -> A4     (0xC,A6)  name_len ptr -> A3
 *   (0x10,A6) uid_ret              (0x14,A6) status_ret   -> D2
 *
 * Only reference: the SVC table entry at 0x00E7BABA.
 *
 * Original address: 0x00e3ead0
 */

#include "proc2/proc2_internal.h"

void PROC2_$NAME_TO_UID(char *name, int16_t *name_len, uid_t *uid_ret, status_$t *status_ret)
{
    int16_t index;               /* D0 */
    proc2_info_t *entry;         /* A2 (biased) */
    uint16_t entry_len;          /* D1 */
    int16_t i;

    /* 0x00E3EAEA-0x00E3EAFC: negative or > 32 -> invalid name, no lock */
    if (*name_len < 0 || *name_len > 0x20) {
        *status_ret = status_$proc2_invalid_process_name;   /* 0x00E3EAF6 */
        return;
    }

    /* 0x00E3EB00-0x00E3EB10 */
    *status_ret = status_$ok;
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3EB12: D0 = alloc ptr; beq not-found */
    index = (int16_t)P2_INFO_ALLOC_PTR;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);                        /* 0x00E3EB18-0x00E3EB24 */

        /* 0x00E3EB28-0x00E3EB30: zero-extended entry+0xBE vs *name_len (re-read) */
        entry_len = entry->name_len;
        if (entry_len == (uint16_t)*name_len) {
            /*
             * 0x00E3EB32-0x00E3EB48: D0 = *name_len - 1; bmi -> match (an
             * empty name matches); dbf compares entry+0x9D+i with name[i-1]
             * for i = 1..len.
             */
            for (i = 0; i < *name_len; i++) {
                if (entry->name[i] != name[i]) {
                    goto next;                               /* 0x00E3EB44 */
                }
            }
            /* 0x00E3EB4C-0x00E3EB66: unlock, copy the UID, exit with status 0 */
            ML_$UNLOCK(PROC2_LOCK_ID);
            uid_ret->high = entry->uid.high;
            uid_ret->low = entry->uid.low;
            return;
        }
next:
        index = (int16_t)entry->next_index;                  /* 0x00E3EB68 */
    }

    /* 0x00E3EB6E-0x00E3EB7C */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status_$proc2_uid_not_found;
}
