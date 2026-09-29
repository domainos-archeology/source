/*
 * PROC2_$PGROUP_INHERIT_INTERNAL - Copy one entry's process group to another
 *
 * Bumps the source group's reference count (when the source is in a group at
 * all) and copies the pgroup table index into the destination entry.  Used by
 * PROC2_$INIT_ENTRY_INTERNAL (0x00E733EC) to let a new process inherit its
 * creator's process group.
 *
 * Parameters:
 *   from - the entry whose process group is being inherited
 *   to   - the entry receiving it
 *
 * Original address: 0x00e4216e (the batch entry 0x00E733EC is the call
 * site in PROC2_$INIT_ENTRY_INTERNAL).  Re-verified against the image;
 * faithful as written.
 *
 * Assembly:
 *   00e4216e  link.w A6,-0x8
 *   00e42172  pea (A5)                       ; PROC2 module base saved/restored
 *   00e4217a  movea.l (0x8,A6),A0            ; A0 = from
 *   00e4217e  tst.w (0x10,A0)                ; from->pgroup_table_idx
 *   00e42182  beq.b 0x00e42198
 *   00e42184  move.w (0x10,A0),D0w
 *   00e4218e  lsl.w #0x3,D0w                 ; * 8 = sizeof(pgroup_entry_t)
 *   00e42194  addq.w #0x1,(0x3f30,A1)        ; PROC2_$DATA.pgroup[idx].ref_count++
 *   00e42198  movea.l (0xc,A6),A1            ; A1 = to
 *   00e4219c  move.w (0x10,A0),(0x10,A1)
 */

#include "proc2/proc2_internal.h"

void PROC2_$PGROUP_INHERIT_INTERNAL(proc2_info_t *from, proc2_info_t *to)
{
    /* 0x00E4217E: only groups other than 0 are reference counted */
    if (from->pgroup_table_idx != 0) {
        /* 0x00E42194: PROC2_$DATA.pgroup[0] is 0xEA551C + 0x3F30 = 0xEA944C */
        PROC2_$DATA.pgroup[from->pgroup_table_idx].ref_count++;
    }

    /* 0x00E4219C */
    to->pgroup_table_idx = from->pgroup_table_idx;
}
