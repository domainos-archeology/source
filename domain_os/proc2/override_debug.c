/*
 * PROC2_$OVERRIDE_DEBUG - Override debug settings
 *
 * Re-emitted from the image (0x00E41722..0x00E4180E, 238 bytes) and
 * verified block by block; the previous text was already faithful.
 *
 * Attaches the calling process as the debugger of the target process,
 * overriding any existing debug relationship. Unlike DEBUG, this does
 * NOT check if the target is already being debugged (there is no
 * entry+0x26 test between 0x00E417A4 and 0x00E417B0).
 *
 * If proc_uid is UID_$NIL, the caller's parent becomes its debugger.
 *
 * Frame (link.w A6,-0x14): (0x8,A6) proc_uid -> A6-0x8, (0xC,A6)
 * status_ret; A6-0xC status, cleared at 0x00E41730.
 *
 * Parameters:
 *   proc_uid   - UID of process to debug (or UID_$NIL for parent)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$proc2_uid_not_found - Target process not found
 *   status_$proc2_permission_denied - ACL check failed
 *
 * Original address: 0x00e41722
 */

#include "proc2/proc2_internal.h"

void PROC2_$OVERRIDE_DEBUG(uid_t *proc_uid, status_$t *status_ret)
{
    uid_t uid;
    status_$t status;
    int16_t target_idx;
    int16_t debugger_idx;
    int8_t flag;
    proc2_info_t *entry;

    /* 0x00E41730-0x00E4173C */
    status = status_$ok;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    /* 0x00E41740-0x00E4174C */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E4174E-0x00E41760: cmpm.l twice against UID_$NIL (0xE1737C) */

    if (uid.high == UID_$NIL.high && uid.low == UID_$NIL.low) {
        /*
         * 0x00E41762-0x00E4178C: UID_$NIL means "let my parent debug me".
         * Unlike PROC2_$DEBUG, which pushes the raw table index, this entry
         * point pushes the caller entry's own +0x1C field as the target:
         *
         *   00e41776  move.w (0x3eb6,A1),D0w      ; P2_PID_TO_INDEX[pid]
         *   00e4177a  mulu.w #0xe4,D0
         *   00e4177e  lea (0x0,A0,D0w),A2         ; caller entry + 0xE4
         *   00e41782  clr.w -(SP)                 ; flag = 0        (arg 3)
         *   00e41784  move.w (-0xc6,A2),-(SP)     ; +0x1E parent    (arg 2)
         *   00e41788  move.w (-0xc8,A2),-(SP)     ; +0x1C self idx  (arg 1)
         *
         * +0x1C holds the entry's own 1-based table index (see
         * proc2/make_orphan.c), so the two entry points agree.
         */
        int16_t current_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
        proc2_info_t *current_entry = P2_INFO_ENTRY(current_idx);

        target_idx = (int16_t)current_entry->self_index;
        debugger_idx = (int16_t)current_entry->parent_pgroup_idx;
        flag = 0;
    } else {
        /* 0x00E4178E-0x00E4179C: D2 = PROC2_$FIND_INDEX(&uid, &status) */
        target_idx = PROC2_$FIND_INDEX(&uid, &status);

        /* 0x00E4179E: tst.l (-0xc,A6) / bne done */
        if (status != status_$ok) {
            goto done;
        }

        /* NOTE: Unlike DEBUG, we do NOT check if already being debugged */
        entry = P2_INFO_ENTRY(target_idx);

        /*
         * 0x00E417B0: pea (-0x4a,A0,D0) -- entry + 0xE4 - 0x4A = entry+0x9A,
         * i.e. &target->level1_pid, NOT the entry base.
         * 0x00E417B4: move.l #0xe20608,-(SP) -- &PROC1_$CURRENT.
         * 0x00E417C2: tst.b D0b / bpl -- negative (0xFF) means allowed.
         */
        if (ACL_$CHECK_DEBUG_RIGHTS((int16_t *)&PROC1_$CURRENT,
                                    (int16_t *)&entry->level1_pid) >= 0) {
            status = status_$proc2_permission_denied;
            goto done;
        }

        /* 0x00E417C8: st -(SP) = TRUE; 0x00E417DC: caller's own index */
        debugger_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
        flag = (int8_t)0xFF;
    }

    /* 0x00E417E2 */
    DEBUG_SETUP_INTERNAL(target_idx, debugger_idx, flag);

done:
    /* 0x00E417F2-0x00E41802 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
