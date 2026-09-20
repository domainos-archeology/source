/*
 * PGROUP_CLEANUP_INTERNAL - Drop a process's process-group references
 *
 * Re-emitted from the image (0x00E420B8..0x00E4216C, 182 bytes).
 *
 * Frame (link.w A6,-0x18; A5 = 0xE7BE84):
 *   (0x8,A6)  entry  -> A2  (UNBIASED entry base)
 *   (0xC,A6)  mode   -> D2  word: 1 = ref count only, 0 = leader counts
 *                            only, anything else = both
 *   A3 = 0xEA551C + pgidx*8, so (0x3F30,A3) = PGROUP_TABLE[pgidx].ref_count
 *   A0 = 0xEA551C + parent*0xE4 (biased parent entry, computed even when
 *        the parent index is 0)
 *
 * Callers: PROC2_$DELETE 0x00E7448C/0x00E7492E, DETACH_FROM_PARENT
 * 0x00E40E6E, PROC2_$CREATE 0x00E72B30, PGROUP_SET_INTERNAL 0x00E41EAE,
 * PROC2_$FORK 0x00E7324A, WAIT_REAP_CHILD 0x00E3FBE2, SET_SESSION_ID
 * 0x00E41CEE.
 *
 * Original address: 0x00e420b8
 */

#include "proc2/proc2_internal.h"

void PGROUP_CLEANUP_INTERNAL(proc2_info_t *entry, int16_t mode)
{
    proc2_info_t *parent;
    proc2_info_t *child;          /* A4 (biased) */
    int16_t child_idx;            /* D0 */
    uint16_t child_pgroup;        /* D1 */

    /* 0x00E420CE: tst.w (0x10,A2) / beq exit */
    if (entry->pgroup_table_idx == 0) {
        return;
    }

    /* 0x00E420E6-0x00E420EE: A0 = parent entry (mulu on entry+0x1E) */
    parent = P2_INFO_ENTRY((int16_t)entry->parent_pgroup_idx);

    /* 0x00E420F2: cmpi.w #0x1,D2w / beq 0x00E42158 */
    if (mode != 1) {
        /*
         * 0x00E420F8-0x00E4211C: the parent, if any, in a different group
         * but the same session -> drop a leader from THIS entry's group.
         */
        if (entry->parent_pgroup_idx != 0 &&
            parent->pgroup_table_idx != entry->pgroup_table_idx &&
            parent->session_id == entry->session_id) {
            PGROUP_DECR_LEADER_COUNT((int16_t)entry->pgroup_table_idx);   /* 0x00E42118 */
        }

        /*
         * 0x00E4211E-0x00E42156: every child (entry+0x20, then +0x22) in a
         * different group but the same session -> drop a leader from the
         * CHILD's group.
         */
        child_idx = (int16_t)entry->first_child_idx;
        while (child_idx != 0) {
            child = P2_INFO_ENTRY(child_idx);                /* 0x00E42124-0x00E42130 */
            child_pgroup = child->pgroup_table_idx;          /* 0x00E42134 */
            if (child_pgroup != entry->pgroup_table_idx &&
                child->session_id == entry->session_id) {
                PGROUP_DECR_LEADER_COUNT((int16_t)child_pgroup);   /* 0x00E4214C */
            }
            child_idx = (int16_t)child->next_child_sibling;  /* 0x00E42152 */
        }
    }

    /* 0x00E42158: tst.w D2w / beq exit */
    if (mode != 0) {
        /* 0x00E4215C: subq.w #1,(0x3f30,A3); 0x00E42160: clr.w (0x10,A2) */
        PGROUP_ENTRY(entry->pgroup_table_idx)->ref_count -= 1;
        entry->pgroup_table_idx = 0;
    }
}
