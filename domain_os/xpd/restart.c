/*
 * xpd/restart.c - XPD_$RESTART (0x00E5B54A, 434 bytes)
 *
 * Resume a stopped target: optionally rewrite the fault frame's PC, the
 * status and the signal the target will see; set or clear the single-step
 * and suspended flags according to `mode`; PROC1_$RESUME it; and, when its
 * ptrace flags2 bit 4 asks for it, wait on the debugger's creation-record
 * eventcount until the target has stopped again, reporting how it stopped.
 *
 * Frame (link.w A6,-0x20; A4 A3 A2 D5 D4 D3 D2 saved):
 *   A6-0x20  4  ecs[1]      the one eventcount pointer EC_$WAITN is given
 *   A6-0x14  4  st          the status being built
 *   A6-0x0C  4  wait_val    the eventcount's value + 1
 *   A6-0x08  8  uid copy
 *   A2  the entry (+0xE4); A3 pc; D3 signal; D4 status; D5 status_ret
 */

#include "xpd/xpd_internal.h"

void XPD_$RESTART(uid_t *proc_uid, uint16_t *mode, int32_t *pc, int16_t *signal,
                  int32_t *status, status_$t *status_ret)
{
    ec_$eventcount_t *ecs[1];           /* A6-0x20 */
    status_$t st;                       /* A6-0x14 */
    int32_t wait_val;                   /* A6-0x0C */
    uid_t uid;                          /* A6-0x08 */
    int16_t idx;                        /* D2 */
    proc2_info_t *entry;                /* A2 */
    xpd_$debug_state_t *state;          /* A0 */
    uint16_t sig;                       /* D0 */
    uint32_t v;                         /* D1 */

    /* 0x00E5B562-0x00E5B59E */
    uid = *proc_uid;
    ML_$LOCK(PROC2_LOCK_ID);
    idx = XPD_$FIND_INDEX(&uid, &st);
    ML_$UNLOCK(PROC2_LOCK_ID);
    if (st != status_$ok) {
        goto done;
    }

    /* 0x00E5B5A2-0x00E5B5C4: no record -> state_unavailable, stored
     * directly */
    entry = XPD_ENTRY(idx);
    state = (xpd_$debug_state_t *)ARCH_VA_TO_PTR(XPD_ENTRY_STATE_VA(entry));
    if (state == NULL) {
        *status_ret = status_$xpd_state_unavailable_for_this_event;
        return;
    }

    /* 0x00E5B5C8-0x00E5B5D4: a pc of 1 keeps the current one */
    if (*pc != 1) {
        XPD_FRAME_PC_SET(state->frame, (uint32_t)*pc);
    }

    /* 0x00E5B5D8-0x00E5B5FA: a changed signal, or signal 0x13 with a
     * status given, replaces the fault status (bit 23 set); the signal
     * word is stored in any case */
    sig = entry->pad_94;
    if (sig != (uint16_t)*signal || (sig == 0x13 && *status != 0)) {
        PROC2_FAULT_PARAM_SET(entry, (uint32_t)*status | 0x00800000u);
    }
    entry->pad_94 = (uint16_t)*signal;

    /* 0x00E5B5FE-0x00E5B640: the jump table at 0x00E5B614 (00 e0 | 00 08 |
     * 00 10 | 00 16): mode 0 goes straight to the status store; 1 clears
     * trace-pending and suspended (`andi.w #0xffed`); 2 sets trace-pending
     * and clears suspended; 3 clears suspended; 4 and up skip the resume */
    switch (*mode) {
    case 0:
        goto done;
    case XPD_RESTART_MODE_CONTINUE:
        entry->flags &= (uint16_t)~(XPD_PF_TRACE_PENDING | XPD_PF_SUSPENDED);
        break;
    case XPD_RESTART_MODE_STEP:
        entry->flags |= XPD_PF_TRACE_PENDING;
        entry->flags &= (uint16_t)~XPD_PF_SUSPENDED;
        break;
    case XPD_RESTART_MODE_STEP_NO_TRACE:
        entry->flags &= (uint16_t)~XPD_PF_SUSPENDED;
        break;
    default:
        goto no_resume;
    }
    /* 0x00E5B630-0x00E5B640 (a word result slot, discarded) */
    PROC1_$RESUME(entry->level1_pid, &st);

no_resume:
    /* 0x00E5B642-0x00E5B648: flags2 bit 4 clear -> report st as it is */
    if ((XPD_PTRACE_OPTS(entry)->flags2 & 0x10) == 0) {
        goto done;
    }

    /* 0x00E5B64C-0x00E5B6C6: read the debugger's cr_rec eventcount + 1,
     * then while the target is not yet suspended but still a debug
     * target, EC_$WAITN on it and re-read */
    ecs[0] = PROC_CR_REC_EC(entry->debugger_idx);
    wait_val = EC_$READ(ecs[0]) + 1;
    while ((entry->flags & XPD_PF_SUSPENDED) == 0 &&
           (entry->flags & XPD_PF_DEBUG_TARGET) != 0) {
        (void)EC_$WAITN(ecs, &wait_val, 1);
        wait_val = EC_$READ(PROC_CR_REC_EC(entry->debugger_idx)) + 1;
    }

    /* 0x00E5B6C8-0x00E5B6EC: still a debug target -> mark the state as
     * handed out and report the fault status, or 0x0901<signal> when
     * there is none; else proc2_proc_not_found */
    if ((entry->flags & XPD_PF_DEBUG_TARGET) != 0) {
        entry->flags |= XPD_PF_STATE_SAVED;
        v = PROC2_FAULT_PARAM_GET(entry);
        if (v == 0) {
            v = (uint32_t)(int32_t)(int16_t)entry->pad_94 + 0x09010000u;
        }
        st = (status_$t)v;
    } else {
        st = status_$proc2_uid_not_found;
    }

done:
    /* 0x00E5B6F4-0x00E5B6F6 */
    *status_ret = st;
}
