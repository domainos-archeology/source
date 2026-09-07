/*
 * PROC2_$DEBUG - Start debugging a process
 *
 * Attaches the calling process as the debugger of the target process.
 * If proc_uid is UID_$NIL, debugs the current process's parent.
 *
 * Parameters:
 *   proc_uid   - UID of process to debug (or UID_$NIL for parent)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$proc2_uid_not_found - Target process not found
 *   status_$proc2_process_already_debugging - Target already has debugger
 *   status_$proc2_permission_denied - ACL check failed
 *
 * Original address: 0x00e41620
 */

#include "proc2/proc2_internal.h"

void PROC2_$DEBUG(uid_t *proc_uid, status_$t *status_ret)
{
    uid_t uid;
    status_$t status;
    int16_t target_idx;
    int16_t debugger_idx;
    int8_t flag;
    proc2_info_t *entry;

    status = status_$ok;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    ML_$LOCK(PROC2_LOCK_ID);

    if (uid.high == UID_$NIL.high && uid.low == UID_$NIL.low) {
        /*
         * 0x00E41660-0x00E4168A: UID_$NIL means "let my parent debug me".
         * The target is the CALLER's own entry and the debugger is the
         * caller's parent (+0x1E), with the flag pushed as a zero word.
         *
         *   00e41660  move.w (0x00e20608).l,D0w   ; PROC1_$CURRENT
         *   00e4166c  add.w D0w,D0w
         *   00e41674  move.w (0x3eb6,A1),D2w      ; P2_PID_TO_INDEX[pid]
         *   00e41678  clr.w -(SP)                 ; flag = 0        (arg 3)
         *   00e41684  move.w (-0xc6,A1),-(SP)     ; +0x1E           (arg 2)
         *   00e41688  move.w D2w,-(SP)            ; caller's index  (arg 1)
         */
        int16_t current_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
        proc2_info_t *current_entry = P2_INFO_ENTRY(current_idx);

        target_idx = current_idx;
        debugger_idx = (int16_t)current_entry->parent_pgroup_idx;
        flag = 0;
    } else {
        /* Find target process by UID */
        target_idx = PROC2_$FIND_INDEX(&uid, &status);

        if (status != status_$ok) {
            goto done;
        }

        entry = P2_INFO_ENTRY(target_idx);

        /* Check if process is already being debugged */
        if (entry->debugger_idx != 0) {
            status = status_$proc2_process_already_debugging;
            goto done;
        }

        /*
         * 0x00E416C2: pea (-0x4a,A0,D2) -- entry + 0xE4 - 0x4A = entry+0x9A,
         * i.e. &target->level1_pid, NOT the entry base.
         * 0x00E416C6: move.l #0xe20608,-(SP) -- &PROC1_$CURRENT.
         * 0x00E416D4: tst.b D0b / bpl -- the boolean result is negative
         * (0xFF) on success, so a non-negative result denies permission.
         */
        if (ACL_$CHECK_DEBUG_RIGHTS((int16_t *)&PROC1_$CURRENT,
                                    (int16_t *)&entry->level1_pid) >= 0) {
            status = status_$proc2_permission_denied;
            goto done;
        }

        /* 0x00E416DA: st -(SP) = TRUE; 0x00E416EE: caller's own index */
        debugger_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
        flag = (int8_t)0xFF;
    }

    /* Set up debug relationship */
    DEBUG_SETUP_INTERNAL(target_idx, debugger_idx, flag);

done:
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
