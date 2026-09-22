/*
 * PROC2_$SIGNAL - Send a signal to a process
 *
 * Re-emitted from the image (0x00E3EFA0..0x00E3F0A4, 262 bytes).
 *
 * Frame (link.w A6,-0x20; A5 = 0xE7BE84):
 *   (0x8,A6)  proc_uid   copied to A6-0x8
 *   (0xC,A6)  signal ptr -> A3 (re-read at each use)
 *   (0x10,A6) param ptr  -> D3 = *ptr, also A6-0xC
 *   (0x14,A6) status_ret <- A6-0x10
 *   A4 = the caller's entry (biased, mulu), A2 = the target's (biased,
 *   muls) -- computed even when FIND_INDEX failed
 *
 * Permission (0x00E3F01C..0x00E3F04C): the target's debugger (+0x26) is
 * the caller (+0x1C); or the target shares the caller's session (+0x5C),
 * the signal is 0x16 and that session is non-zero; or
 * ACL_$CHECK_FAULT_RIGHTS(&caller->level1_pid, &target->level1_pid) is
 * TRUE.  Delivery happens only when the status is exactly zero (a zombie
 * passes the permission check but is not delivered to).
 *
 * The audit call at 0x00E3F088 is unconditional and receives the FINAL
 * status; it is made after the caller's status has been stored.
 *
 * Callers: PROC2_$QUIT 0x00E3F14C, SVC table 0x00E7BA4A.
 *
 * Original address: 0x00e3efa0
 */

#include "proc2/proc2_internal.h"

void PROC2_$SIGNAL(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                   status_$t *status_ret)
{
    uid_t uid;                   /* A6-0x8 */
    uint32_t param_copy;         /* D3 / A6-0xC */
    status_$t status;            /* A6-0x10 */
    int16_t index;               /* D2 */
    proc2_info_t *target;        /* A2 */
    proc2_info_t *current;       /* A4 */

    /* 0x00E3EFAE-0x00E3EFC4 */
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;
    param_copy = *param;

    /* 0x00E3EFC8-0x00E3EFD4 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3EFD6-0x00E3EFE4 */
    index = PROC2_$FIND_INDEX(&uid, &status);

    /* 0x00E3EFE6-0x00E3F00A */
    current = P2_INFO_ENTRY((int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT));
    target = P2_INFO_ENTRY(index);

    /* 0x00E3F00E-0x00E3F01A: status 0 or status_$proc2_zombie */
    if (status == status_$ok || status == status_$proc2_zombie) {
        /* 0x00E3F01C-0x00E3F024: target+0x26 == caller+0x1C */
        if (target->debugger_idx != current->self_index) {
            /* 0x00E3F026-0x00E3F038 */
            if (!(target->session_id == current->session_id &&
                  *signal == 0x16 &&
                  target->session_id != 0)) {
                /* 0x00E3F03A-0x00E3F04C: pea (-0x4a,A2), pea (-0x4a,A4) ->
                 * (&caller->level1_pid, &target->level1_pid); bpl -> denied */
                if (ACL_$CHECK_FAULT_RIGHTS(&current->level1_pid,
                                            &target->level1_pid) >= 0) {
                    status = status_$proc2_permission_denied;   /* 0x00E3F06A */
                    goto done;
                }
            }
        }
        /* 0x00E3F04E-0x00E3F064: tst.l status / bne; deliver on zero only */
        if (status == status_$ok) {
            PROC2_$DELIVER_SIGNAL_INTERNAL(index, *signal, param_copy, &status);
        }
    }

done:
    /* 0x00E3F072-0x00E3F084 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;

    /* 0x00E3F088-0x00E3F098: (1, index, *signal, param, status), result slot */
    PROC2_$LOG_SIGNAL_EVENT(1, index, (uint16_t)*signal, param_copy, status);
}
