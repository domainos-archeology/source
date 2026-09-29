/*
 * PROC2_$GET_UPIDS - Return a process's Unix ids by UID
 *
 * Re-emitted from the image (0x00E738A8..0x00E73966, 192 bytes).
 *
 * Frame (link.w A6,-0x18; A5 = 0xE86054, unused):
 *   (0x8,A6)  proc_uid    copied to A6-0x8
 *   (0xC,A6)  upid_ret    <- D4 = entry+0x16
 *   (0x10,A6) uppid_ret   <- D3 = P2[entry+0x1E]->upid, or 1 without a parent
 *   (0x14,A6) upgid_ret   <- D2 = PROC2_$DATA.pgroup[entry+0x10].upgid, or 0
 *   (0x18,A6) status_ret  <- A6-0xC (from PROC2_$FIND_INDEX)
 *
 * The three result registers are written only on the success path; when
 * PROC2_$FIND_INDEX fails (0x00E738E4 bne 0x00E73938) the stores at
 * 0x00E73948..0x00E73954 still happen with whatever D2/D3/D4 held on
 * entry, so the outputs are indeterminate in that case.
 *
 * NOTE: the second output is the PARENT's upid and the third the process
 * group id; the old prototype had them the other way round.
 *
 * Original address: 0x00e738a8
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_UPIDS(uid_t *proc_uid, uint16_t *upid_ret, uint16_t *uppid_ret,
                      uint16_t *upgid_ret, status_$t *status_ret)
{
    uid_t uid;               /* A6-0x8 */
    status_$t status;        /* A6-0xC */
    int16_t index;           /* D0 */
    uint16_t upid;           /* D4 -- indeterminate on the failure path */
    uint16_t uppid;          /* D3 */
    uint16_t upgid;          /* D2 */
    proc2_info_t *entry;
    proc2_info_t *parent;

    /* 0x00E738B6-0x00E738BE */
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    /* 0x00E738C2-0x00E738CE */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E738D0-0x00E738DE */
    index = PROC2_$FIND_INDEX(&uid, &status);

    /* 0x00E738E0: tst.l (-0xc,A6) -- the whole longword */
    if (status == status_$ok) {
        /* 0x00E738E6-0x00E738F2 (muls) */
        entry = P2_INFO_ENTRY(index);

        /* 0x00E738F6: entry+0x16 */
        upid = entry->upid;

        /* 0x00E738FA-0x00E73918: entry+0x1E */
        if (entry->parent_pgroup_idx != 0) {
            parent = P2_INFO_ENTRY((int16_t)entry->parent_pgroup_idx);   /* mulu */
            uppid = parent->upid;                                        /* 0x00E73912 */
        } else {
            uppid = 1;                                                   /* 0x00E73918 */
        }

        /* 0x00E7391A-0x00E73936: entry+0x10 */
        if (entry->pgroup_table_idx != 0) {
            upgid = PROC2_$DATA.pgroup[entry->pgroup_table_idx].upgid;        /* 0x00E73930 */
        } else {
            upgid = 0;                                                   /* 0x00E73936 */
        }
    }

    /* 0x00E73938-0x00E73944 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E73944-0x00E7395A: unconditional stores */
    *upid_ret = upid;
    *uppid_ret = uppid;
    *upgid_ret = upgid;
    *status_ret = status;
}
