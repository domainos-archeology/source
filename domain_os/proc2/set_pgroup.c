/*
 * PROC2_$SET_PGROUP - Move a process into a process group (setpgid)
 *
 * Re-emitted from the image (0x00E410C8..0x00E4120A, 324 bytes).
 *
 * Frame (link.w A6,-0x24; A5 = 0xE7BE84):
 *   (0x8,A6)  proc_uid   copied to A6-0x10 and again to A6-0x8
 *   (0xC,A6)  new_upgid ptr -> D2 = *ptr
 *   (0x10,A6) status_ret   <- A6-0x14
 *   A3 = caller's entry (biased, mulu), A2 = target's entry (biased, muls)
 *
 * Permission (bead source-e8c8): the target is the caller itself
 * (+0x1C == +0x1C), or the caller is its parent (+0x1C == target +0x1E) and
 * the target is not an orphan (0x1000) unless debugged (0x0008), and both
 * share a session.  Then a non-zero group must not be the target's own
 * session id, and, unless it equals the target's upid, must already exist
 * in the caller's session.
 *
 * Only reference: the SVC table entry at 0x00E7B77E.
 *
 * Original address: 0x00e410c8
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_PGROUP(uid_t *proc_uid, uint16_t *new_upgid, status_$t *status_ret)
{
    uid_t uid;                   /* A6-0x8 (via A6-0x10) */
    uint16_t upgid;              /* D2 */
    int16_t target_idx;          /* D0 */
    int16_t pgroup_idx;
    proc2_info_t *target;        /* A2 */
    proc2_info_t *current;       /* A3 */
    status_$t status;            /* A6-0x14 */

    /* 0x00E410D6-0x00E410F2 */
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;
    upgid = *new_upgid;

    /* 0x00E410F6-0x00E41102 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E41104-0x00E41116: tst.l status / bne done */
    target_idx = PROC2_$FIND_INDEX(&uid, &status);
    if (status != status_$ok) {
        goto done;
    }

    /* 0x00E4111A-0x00E4113E */
    current = P2_INFO_ENTRY((int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT));
    target = P2_INFO_ENTRY(target_idx);

    /* 0x00E41142-0x00E4114A: the target is me */
    if (current->self_index != target->self_index) {
        /* 0x00E4114C: am I its parent? */
        if (current->self_index != target->parent_pgroup_idx) {
            status = status_$proc2_uid_not_found;            /* 0x00E41152 */
            goto done;
        }
        /* 0x00E4115E-0x00E4116C: btst #12 set and btst #3 clear -> denied */
        if ((target->flags & PROC2_FLAG_ORPHAN) != 0 &&
            (target->flags & PROC2_FLAG_DEBUG) == 0) {
            status = status_$proc2_permission_denied;        /* 0x00E4116E */
            goto done;
        }
        /* 0x00E41178-0x00E41180: target+0x5C vs caller+0x5C */
        if (target->session_id != current->session_id) {
            goto different_session;                          /* 0x00E411C8 */
        }
    }

    /* 0x00E41182: tst.w D2w / beq set */
    if (upgid != 0) {
        /* 0x00E41186-0x00E4118E: a session leader may not move */
        if (target->session_id == target->upid) {
            goto different_session;
        }
        /* 0x00E41190-0x00E41198: (redundant zero test) then own upid -> set */
        if (upgid != 0 && upgid != target->upid) {
            /*
             * 0x00E4119A-0x00E411C6: the group must exist in the CALLER's
             * session: zero-extended PGROUP[idx].session_id (read even for
             * idx 0) vs sign-extended caller+0x5C, then idx != 0.
             */
            pgroup_idx = PGROUP_FIND_BY_UPGID(upgid);
            if ((int32_t)(uint32_t)PGROUP_ENTRY(pgroup_idx)->session_id !=
                (int32_t)(int16_t)current->session_id) {
                goto different_session;
            }
            if (pgroup_idx == 0) {
                goto different_session;
            }
        }
    }

    /* 0x00E411D2-0x00E411E2: PGROUP_SET_INTERNAL(target base, upgid, &status) */
    PGROUP_SET_INTERNAL(target, upgid, &status);

    /* 0x00E411E6-0x00E411EA: a zero group also clears the session id */
    if (upgid == 0) {
        target->session_id = 0;
    }
    goto done;

different_session:
    status = status_$proc2_pgroup_in_different_session;      /* 0x00E411C8 */

done:
    /* 0x00E411EE-0x00E411FE */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
