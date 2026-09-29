/*
 * PROC2_$DETACH_FROM_PARENT - Detach a process from its parent's child list
 *
 * Re-emitted from the image (0x00E40DF4..0x00E40ECC, 218 bytes).
 *
 * Unlinks the child from its parent's sibling chain and clears the parent
 * link.  A zombie is then released: its process-group reference is
 * dropped, it is unlinked from the allocated list and pushed onto the free
 * list.  A live process is merely marked orphaned.
 *
 * Frame (link.w A6,-0xC):
 *   (0x8,A6)  child_idx         word -> D2
 *   (0xA,A6)  prev_sibling_idx  word -> D0 (0 = child is the first child)
 *   A5 = 0xE7BE84: (0x1E0,A5) PROC2_$UNWIRED_DATA.info_alloc_ptr, (0x1E2,A5) PROC2_$UNWIRED_DATA.free_list_head
 *
 * The child is A2 = 0xEA551C + idx*0xE4 = entry + 0xE4: (-0xC6,A2) = +0x1E
 * parent, (-0xC4) = +0x20 first child, (-0xC2) = +0x22 next sibling,
 * (-0xBA) = +0x2A flags, (-0xD2) = +0x12 next_index, (-0xD0) = +0x14 prev.
 *
 * Callers: PROC2_$DELETE 0x00E744D2/0x00E746BC, PROC2_$WAIT 0x00E3F92C/
 * 0x00E3F9A8, PROC2_$MAKE_ORPHAN 0x00E40DCC.
 *
 * Original address: 0x00e40df4
 */

#include "proc2/proc2_internal.h"

void PROC2_$DETACH_FROM_PARENT(int16_t child_idx, int16_t prev_sibling_idx)
{
    proc2_info_t *entry;      /* A2 (biased) */
    proc2_info_t *other;

    /* 0x00E40E02-0x00E40E16 */
    entry = P2_INFO_ENTRY(child_idx);

    /* 0x00E40E1A: tst.w (-0xc6,A2) -- entry+0x1E */
    if (entry->parent_pgroup_idx != 0) {
        /* 0x00E40E20: tst.w D0w */
        if (prev_sibling_idx == 0) {
            /* 0x00E40E24-0x00E40E30: parent->first_child = entry->next_sibling (mulu) */
            other = P2_INFO_ENTRY((int16_t)entry->parent_pgroup_idx);
            other->first_child_idx = entry->next_child_sibling;
        } else {
            /* 0x00E40E38-0x00E40E42: prev->next_sibling = entry->next_sibling (muls) */
            other = P2_INFO_ENTRY(prev_sibling_idx);
            other->next_child_sibling = entry->next_child_sibling;
        }
        /* 0x00E40E48: clr.w (-0xc6,A2) */
        entry->parent_pgroup_idx = 0;
    } else {
        /*
         * 0x00E40E4E-0x00E40E58: no parent link -- CRASH_SYSTEM with the
         * code-region cell at 0x00E40DF0 (00 19 00 13 =
         * status_$proc2_internal_error, shared with PROC2_$MAKE_ORPHAN's
         * 0x00E40DA2 reference).  If it returns, execution simply falls
         * into the zombie test below.
         */
        CRASH_SYSTEM(&PROC2_Internal_Error);
    }

    /* 0x00E40E5A-0x00E40E62: btst.l #0xd on the flags word */
    if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
        /* 0x00E40E64-0x00E40E72: PGROUP_CLEANUP_INTERNAL(entry, 1), result slot */
        PGROUP_CLEANUP_INTERNAL(entry, 1);

        /* 0x00E40E74-0x00E40E94: unlink from the allocated list */
        if (entry->pad_14 == 0) {
            PROC2_$UNWIRED_DATA.info_alloc_ptr = entry->next_index;          /* 0x00E40E7A */
        } else {
            other = P2_INFO_ENTRY((int16_t)entry->pad_14);  /* mulu */
            other->next_index = entry->next_index;          /* 0x00E40E94 */
        }

        /*
         * 0x00E40E9A-0x00E40EAC: P2[next]->pad_14 = entry->pad_14,
         * UNCONDITIONALLY (a zero next_index writes entry(0)+0x14).
         * Entry 0 is not part of PROC2_$DATA: in the image the store lands
         * at 0xEA544C, inside the preceding XPD_$DATA segment, and in our
         * link 0xE4 bytes before the PROC2_$DATA block, which is the tail of
         * the XPD_$DATA block linked directly before it (see proc2/proc2.h;
         * source-c6cy).
         */
        other = P2_INFO_ENTRY((int16_t)entry->next_index);
        other->pad_14 = entry->pad_14;

        /* 0x00E40EB2-0x00E40EB8: push onto the free list */
        entry->next_index = PROC2_$UNWIRED_DATA.free_list_head;
        PROC2_$UNWIRED_DATA.free_list_head = (uint16_t)child_idx;
    } else {
        /* 0x00E40EBE: bset.b #0x7,(-0xba,A2) -- HIGH byte bit 7 = 0x8000 */
        entry->flags |= 0x8000;
    }
}
