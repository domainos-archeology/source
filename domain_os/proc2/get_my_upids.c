/*
 * PROC2_$GET_MY_UPIDS - Return the calling process's Unix ids
 *
 * Re-emitted from the image (0x00E73968..0x00E739F4, 142 bytes).
 *
 * Frame (link.w A6,-0x10; A5 = 0xE86054, unused):
 *   (0x8,A6)  upid_ret   -> A3  <- entry+0x16                (0x00E739A8)
 *   (0xC,A6)  uppid_ret  -> A1  <- P2[entry+0x1E]->upid, or 1 when the
 *                                  entry has no parent      (0x00E739C4/CA)
 *   (0x10,A6) upgid_ret  -> A2  <- PROC2_$DATA.pgroup[entry+0x10].upgid, or 0
 *                                  when it is in no group   (0x00E739E4/EA)
 *
 * The entry is A0 = 0xEA551C + idx*0xE4 = entry + 0xE4: (-0xCE) = +0x16
 * upid, (-0xC6) = +0x1E parent index, (-0xD4) = +0x10 pgroup index.  The
 * group lookup is (0x3F34,A4) with A4 = 0xEA551C + idx*8, i.e. the upgid
 * word (+4) of the 8-byte pgroup record at 0xEA944C + idx*8.
 *
 * NOTE: the second output is the PARENT's upid and the third the process
 * group id; the old prototype had them the other way round.
 *
 * Original address: 0x00e73968
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_MY_UPIDS(uint16_t *upid_ret, uint16_t *uppid_ret, uint16_t *upgid_ret)
{
    proc2_info_t *entry;
    proc2_info_t *parent;

    /* 0x00E7397E-0x00E739A4 */
    entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E739A8: entry+0x16 */
    *upid_ret = entry->upid;

    /* 0x00E739AC-0x00E739CA: entry+0x1E */
    if (entry->parent_pgroup_idx != 0) {
        parent = P2_INFO_ENTRY((int16_t)entry->parent_pgroup_idx);   /* mulu */
        *uppid_ret = parent->upid;                                   /* 0x00E739C4 */
    } else {
        *uppid_ret = 1;                                              /* 0x00E739CA */
    }

    /* 0x00E739CE-0x00E739EA: entry+0x10 */
    if (entry->pgroup_table_idx != 0) {
        *upgid_ret = PROC2_$DATA.pgroup[entry->pgroup_table_idx].upgid;   /* 0x00E739E4 */
    } else {
        *upgid_ret = 0;                                              /* 0x00E739EA */
    }
}
