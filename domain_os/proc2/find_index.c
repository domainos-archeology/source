/*
 * PROC2_$FIND_INDEX - Find process table index by UID
 *
 * Walks the allocated list (head P2_INFO_ALLOC_PTR, link entry+0x12)
 * comparing the eight UID bytes.  Returns the index in D0 (no result
 * slot: callers do `pea status; move.l uid,-(SP); bsr; addq #8`).
 *
 * Parameters:
 *   proc_uid   (0x8,A6) UID to look up (copied to (-0x8,A6) first)
 *   status_ret (0xC,A6) status_$ok, status_$proc2_zombie, or
 *              status_$proc2_uid_not_found
 *
 * Returns: the matching index; when nothing matches, whatever index value
 * ended the walk (0 -- the last next_index, or P2_INFO_ALLOC_PTR itself).
 *
 * Original address: 0x00e4068e (116 bytes)
 * A5 = 0xE7BE84; (0x1E0,A5) = 0xE7C064 P2_INFO_ALLOC_PTR.
 * A1 = 0xEA551C + idx*0xE4 = entry + 0xE4:
 *   (-0xE4,A1) = +0x00 uid   (-0xBA,A1) = +0x2A flags   (-0xD2,A1) = +0x12 next_index
 */

#include "proc2/proc2_internal.h"

int16_t PROC2_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret)
{
    uid_t key;               /* (-0x8,A6) */
    int16_t index;           /* D0w */
    proc2_info_t *entry;

    /* 0x00E406A4..0x00E406A8 */
    key = *proc_uid;

    /* 0x00E406AC..0x00E406B0 */
    index = (int16_t)P2_INFO_ALLOC_PTR;
    if (index != 0) {
        do {
            entry = P2_INFO_ENTRY(index);
            /* 0x00E406CE..0x00E406D4 cmpm.l x2 */
            if (entry->uid.high == key.high && entry->uid.low == key.low) {
                /* 0x00E406D6..0x00E406EA: btst.l #0xd of flags */
                if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
                    *status_ret = status_$proc2_zombie;
                } else {
                    *status_ret = status_$ok;
                }
                return index;
            }
            /* 0x00E406EC..0x00E406F0 */
            index = (int16_t)entry->next_index;
        } while (index != 0);
    }

    /* 0x00E406F2 */
    *status_ret = status_$proc2_uid_not_found;
    return index;
}
