/*
 * PROC2_$MAKE_ORPHAN - Detach a process from its parent
 *
 * Removes the process from its parent's child list, so it no longer has a
 * parent.  A process that already has no parent gets
 * status_$proc2_already_orphan; a zombie is allowed to be orphaned (the
 * status_$proc2_zombie returned by PROC2_$FIND_INDEX is folded to ok).
 *
 * Parameters:
 *   proc_uid   - Pointer to process UID to orphan
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00E40D1A (212 bytes)
 *
 * Full instruction trace:
 *   00e40d1a  link.w A6,-0x14
 *   00e40d1e  movem.l {A5 A2 D3 D2},-(SP)
 *   00e40d22  lea (0xe7be84).l,A5        ; PROC2 module data base (unused here)
 *   00e40d28  movea.l (0x8,A6),A0        ; proc_uid
 *   00e40d2c  move.l (A0)+,(-0x8,A6)     ; uid_copy = *proc_uid (8 bytes)
 *   00e40d30  move.l (A0)+,(-0x4,A6)
 *   00e40d34  subq.l #0x2,SP             ; ML_$LOCK's Pascal result slot
 *   00e40d36  move.w #0x4,-(SP)          ; PROC2_LOCK_ID
 *   00e40d3a  jsr 0x00e20b12.l           ; ML_$LOCK
 *   00e40d40  addq.w #0x4,SP
 *   00e40d42  pea (-0xc,A6)              ; &status
 *   00e40d46  pea (-0x8,A6)              ; &uid_copy
 *   00e40d4a  bsr.w 0x00e4068e           ; PROC2_$FIND_INDEX -> D0
 *   00e40d4e  addq.w #0x8,SP
 *   00e40d50  move.w D0w,D3w             ; target_idx
 *   00e40d52  cmpi.l #0x19000e,(-0xc,A6) ; status_$proc2_zombie?
 *   00e40d5a  bne.b 0x00e40d60
 *   00e40d5c  clr.l (-0xc,A6)            ; ... treat as ok
 *   00e40d60  tst.l (-0xc,A6)
 *   00e40d64  bne.b 0x00e40dd0
 *   00e40d66  move.w D3w,D0w
 *   00e40d68  movea.l #0xea551c,A0
 *   00e40d6e  muls.w #0xe4,D0
 *   00e40d72  lea (0x0,A0,D0),A2         ; A2 = P2_INFO_ENTRY(target_idx) + 0xE4
 *   00e40d76  move.w (-0xc6,A2),D2w      ; target->parent_pgroup_idx (+0x1E)
 *   00e40d7a  bne.b 0x00e40d86
 *   00e40d7c  move.l #0x190014,(-0xc,A6) ; status_$proc2_already_orphan
 *   00e40d84  bra.b 0x00e40dd0
 *   00e40d86  move.w D2w,D0w
 *   00e40d88  muls.w #0xe4,D0
 *   00e40d8c  lea (0x0,A0,D0),A1         ; A1 = parent entry + 0xE4
 *   00e40d90  move.w (-0xc4,A1),D2w      ; parent->first_child_idx (+0x20)
 *   00e40d94  cmp.w D2w,D3w
 *   00e40d96  bne.b 0x00e40dae           ; not the head -> walk the list
 *   00e40d98  clr.w D2w                  ; head: predecessor index 0
 *   00e40d9a  bra.b 0x00e40dc8
 *   00e40d9c  move.w (-0xc2,A0),D2w      ; scan = scan->next_child_sibling
 *   00e40da0  bne.b 0x00e40dae
 *   00e40da2  pea (0x4c,PC)              ; -> the cell at 0x00E40DF0
 *   00e40da6  jsr 0x00e1e700.l           ; CRASH_SYSTEM
 *   00e40dac  addq.w #0x4,SP
 *   00e40dae  move.w D2w,D0w
 *   00e40db0  movea.l #0xea551c,A1
 *   00e40db6  muls.w #0xe4,D0
 *   00e40dba  lea (0x0,A1,D0),A0         ; A0 = scan entry + 0xE4
 *   00e40dbe  move.w (-0xc2,A0),D0w      ; scan->next_child_sibling (+0x22)
 *   00e40dc2  cmp.w (-0xc8,A2),D0w       ; target->owner_session (+0x1C)
 *   00e40dc6  bne.b 0x00e40d9c
 *   00e40dc8  move.w D2w,-(SP)           ; predecessor index
 *   00e40dca  move.w D3w,-(SP)           ; target index
 *   00e40dcc  bsr.b 0x00e40df4           ; PROC2_$DETACH_FROM_PARENT
 *   00e40dce  addq.w #0x4,SP
 *   00e40dd0  subq.l #0x2,SP / move.w #0x4,-(SP) / jsr 0x00e20b62.l  ; ML_$UNLOCK
 *   00e40ddc  movea.l (0xc,A6),A0 / move.l (-0xc,A6),(A0)            ; *status_ret
 *
 * Note on the loop test at 0x00E40DC2: the predecessor search compares the
 * scanned entry's next_child_sibling against the TARGET's field at +0x1C
 * (proc2_info_t.owner_session), not against target_idx itself.  Every other
 * use of +0x1C is as a 1-based entry index (PROC_FORK_EC / PROC_CR_REC_EC
 * are indexed by it, PROC2_$INIT sets entry 1's copy to 1, and PROC2_$FORK
 * only ever reads it), so the field holds the entry's own table index,
 * assigned once when the slot is initialised.  That makes this test
 * equivalent to comparing against target_idx.  The field keeps its existing
 * name here; renaming it is tracked separately.
 */

#include "proc2/proc2_internal.h"

/*
 * Status cell passed to CRASH_SYSTEM by `pea (0x4c,PC)` at 0x00E40DA2.
 * The cell is the longword at 0x00E40DF0 = 0x00190013,
 * "OS / process manager (level 2): internal error".
 */
static const status_$t proc2_$internal_error_00e40df0 =
    status_$proc2_internal_error;

void PROC2_$MAKE_ORPHAN(uid_t *proc_uid, status_$t *status_ret)
{
    proc2_info_t *target;
    proc2_info_t *parent;
    int16_t target_idx;
    int16_t parent_idx;
    int16_t scan_idx;
    status_$t status;
    uid_t uid_copy;

    /* 0x00E40D2C: the UID is copied into the frame before the lock */
    uid_copy = *proc_uid;

    /* 0x00E40D3A */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E40D4A */
    target_idx = PROC2_$FIND_INDEX(&uid_copy, &status);

    /* 0x00E40D52: a zombie may still be orphaned */
    if (status == status_$proc2_zombie) {
        status = status_$ok;
    }

    /* 0x00E40D60 */
    if (status == status_$ok) {
        /* 0x00E40D72 */
        target = P2_INFO_ENTRY(target_idx);

        /* 0x00E40D76 */
        parent_idx = (int16_t)target->parent_pgroup_idx;

        if (parent_idx == 0) {
            /* 0x00E40D7C */
            status = status_$proc2_already_orphan;
        } else {
            /* 0x00E40D8C */
            parent = P2_INFO_ENTRY(parent_idx);

            /* 0x00E40D90 */
            scan_idx = (int16_t)parent->first_child_idx;

            /* 0x00E40D94 */
            if (target_idx == scan_idx) {
                /* 0x00E40D98: we are the head; no predecessor */
                scan_idx = 0;
            } else {
                for (;;) {
                    /* 0x00E40DAE..0x00E40DC6 */
                    if ((int16_t)P2_INFO_ENTRY(scan_idx)->next_child_sibling ==
                        (int16_t)target->owner_session) {
                        break;
                    }

                    /* 0x00E40D9C */
                    scan_idx =
                        (int16_t)P2_INFO_ENTRY(scan_idx)->next_child_sibling;

                    if (scan_idx == 0) {
                        /*
                         * 0x00E40DA2: ran off the end of the parent's child
                         * list.  CRASH_SYSTEM does not return; the original
                         * nevertheless falls through into the test above with
                         * scan_idx == 0.
                         */
                        CRASH_SYSTEM(&proc2_$internal_error_00e40df0);
                    }
                }
            }

            /* 0x00E40DC8: (target_idx, predecessor_idx) */
            PROC2_$DETACH_FROM_PARENT(target_idx, scan_idx);
        }
    }

    /* 0x00E40DD0 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E40DDC */
    *status_ret = status;
}
