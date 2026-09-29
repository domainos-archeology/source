/*
 * PROC2_$DEBUG - Start debugging a process
 *
 * Re-emitted from the image (0x00E41620..0x00E41720, 258 bytes).
 *
 * Attaches the calling process as the debugger of the target process.
 * If proc_uid is UID_$NIL the CALLER becomes the target and its parent
 * becomes the debugger (no rights check, flag false).
 *
 * Frame (link.w A6,-0x10):
 *   (0x8,A6)  proc_uid    pointer, copied to A6-0x8 (uid_t)
 *   (0xC,A6)  status_ret  pointer
 *   A6-0xC    status (cleared at 0x00E4162E)
 *
 * Status codes:
 *   status_$ok
 *   status_$proc2_uid_not_found               (from PROC2_$FIND_INDEX)
 *   status_$proc2_process_already_debugging   0x00190011 (0x00E416B8)
 *   status_$proc2_permission_denied           0x00190012 (0x00E416FC)
 *
 * Original address: 0x00e41620
 */

#include "proc2/proc2_internal.h"

void PROC2_$DEBUG(uid_t *proc_uid, status_$t *status_ret)
{
    uid_t uid;               /* A6-0x8 */
    status_$t status;        /* A6-0xC */
    int16_t target_idx;      /* D3 / D2 */
    int16_t debugger_idx;
    int8_t flag;
    proc2_info_t *entry;

    /* 0x00E4162E-0x00E4163A */
    status = status_$ok;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    /* 0x00E4163E-0x00E4164A: result slot + word 4 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E4164C-0x00E4165E: cmpm.l twice against UID_$NIL (0xE1737C) */
    if (uid.high == UID_$NIL.high && uid.low == UID_$NIL.low) {
        /*
         * 0x00E41660-0x00E4168A: UID_$NIL means "let my parent debug me".
         *
         *   00e41660  move.w (0x00e20608).l,D0w   ; PROC1_$CURRENT
         *   00e4166c  add.w D0w,D0w
         *   00e4166e  subq.l #0x2,SP              ; result slot
         *   00e41674  move.w (0x3eb6,A1),D2w      ; PROC2_$DATA.pid_to_index[pid]
         *   00e41678  clr.w -(SP)                 ; flag = 0        (arg 3)
         *   00e41684  move.w (-0xc6,A1),-(SP)     ; entry+0x1E      (arg 2)
         *   00e41688  move.w D2w,-(SP)            ; caller's index  (arg 1)
         *   00e4168a  bra.b 0x00e416f4            ; -> DEBUG_SETUP_INTERNAL
         */
        int16_t current_idx = (int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT];
        proc2_info_t *current_entry = P2_INFO_ENTRY(current_idx);

        target_idx = current_idx;
        debugger_idx = (int16_t)current_entry->parent_pgroup_idx;
        flag = 0;
    } else {
        /* 0x00E4168C-0x00E4169A: D3 = PROC2_$FIND_INDEX(&uid, &status) */
        target_idx = PROC2_$FIND_INDEX(&uid, &status);

        /* 0x00E4169C: tst.l (-0xc,A6) / bne done */
        if (status != status_$ok) {
            goto done;
        }

        /* 0x00E416A2-0x00E416AE */
        entry = P2_INFO_ENTRY(target_idx);

        /* 0x00E416B2: tst.w (-0xbe,A1) -- entry+0x26 */
        if (entry->debugger_idx != 0) {
            status = status_$proc2_process_already_debugging;   /* 0x00E416B8 */
            goto done;
        }

        /*
         * 0x00E416C2: pea (-0x4a,A0,D2) -- entry+0x9A, &level1_pid (arg 2)
         * 0x00E416C6: move.l #0xe20608,-(SP) -- &PROC1_$CURRENT (arg 1)
         * 0x00E416D4: tst.b D0b / bpl -- the boolean result is 0xFF on
         * success, so a non-negative result denies permission.
         */
        if (ACL_$CHECK_DEBUG_RIGHTS((int16_t *)&PROC1_$CURRENT,
                                    (int16_t *)&entry->level1_pid) >= 0) {
            status = status_$proc2_permission_denied;           /* 0x00E416FC */
            goto done;
        }

        /*
         * 0x00E416D8-0x00E416F2:
         *   subq.l #0x2,SP              ; result slot
         *   st -(SP)                    ; flag = TRUE     (arg 3)
         *   move.w (0x3eb6,A0),-(SP)    ; caller's index  (arg 2)
         *   move.w D3w,-(SP)            ; target index    (arg 1)
         */
        debugger_idx = (int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT];
        flag = (int8_t)0xFF;
    }

    /* 0x00E416F4 */
    DEBUG_SETUP_INTERNAL(target_idx, debugger_idx, flag);

done:
    /* 0x00E41704-0x00E41714 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
