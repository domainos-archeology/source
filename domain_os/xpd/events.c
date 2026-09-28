/*
 * xpd/events.c - event capture and delivery
 *
 *   XPD_$CAPTURE_FAULT       0x00E5B1EE  860 bytes (+ nested xpd_$exec_event
 *                                        at 0x00E5B188, 102 bytes)
 *   XPD_$GET_EC              0x00E5BDC2  102 bytes
 *   XPD_$GET_EVENT_AND_DATA  0x00E5BE28  176 bytes
 *   XPD_$CONTINUE_PROC       0x00E5BED8  120 bytes
 *   XPD_$SET_ENABLE          0x00E5BF50  172 bytes
 */

#include "xpd/xpd_internal.h"

/*
 * xpd_$exec_event (0x00E5B188) - nested procedure of XPD_$CAPTURE_FAULT
 *
 * Decides whether an exec / invoke (event code 2) is reported.  It takes a
 * COPY of the whole 0xE4-byte process entry (0x00E5B19A `moveq #0x38` /
 * `move.l (A2)+,(A3)+`: 57 longwords into A6-0xE8) and reaches the parent's
 * frame through the static link (0x00E5B190 `movea.l (A6),A0`): the SR/PC
 * frame pointer (-0x18), the register block (-0x14), the event code
 * (-0x22C) and the caller's `signal` argument (0x10).  Those four are the
 * explicit parameters here.
 *
 * Effects: the fault frame's PC is replaced by the target's A0 (register
 * block +0x20, the new image's entry point).  Then, with the copied ptrace
 * flags: bit <code> set -> true; else bits 2 and 7 of flags2 both set ->
 * event code 0, *signal = 5, bit 7 cleared IN THE COPY ONLY (0x00E5B1DA -
 * the entry itself is never written back), true; else false.
 */
static int8_t xpd_$exec_event(proc2_info_t *entry, xpd_$frame_t *frame,
                              uint32_t *regs, uint16_t *event_code,
                              uint16_t *signal)
{
    proc2_info_t copy;                  /* A6-0xE8 */
    xpd_$ptrace_opts_t *opts;

    /* 0x00E5B196-0x00E5B19E */
    copy = *entry;
    opts = XPD_PTRACE_OPTS(&copy);

    /* 0x00E5B1A4-0x00E5B1AC */
    XPD_FRAME_PC_SET(frame, regs[8]);

    /* 0x00E5B1B2-0x00E5B1BE */
    if ((opts->flags >> *event_code) & 1) {
        return -1;
    }
    /* 0x00E5B1C0-0x00E5B1E0 */
    if ((opts->flags2 & 0x04) != 0 && (int8_t)opts->flags2 < 0) {
        *event_code = 0;
        *signal = 5;
        opts->flags2 &= 0x7F;
        return -1;
    }
    return 0;
}

/*
 * XPD_$CAPTURE_FAULT - stop the current process for its debugger
 *
 * Called by PROC2_$DELIVER_FIM (the only caller, 0x00E3EEAA) with the
 * ADDRESSES of its two frame cells holding the register block and the
 * SR/PC frame, the signal number and the fault status.  The status
 * selects the event; the target's ptrace options decide whether the
 * event is captured (the debugger is told and the target stops until it
 * is continued), ignored (the quit inhibit for this AS is cleared and
 * *signal is zeroed), or passed through untouched.
 *
 * Frame (link.w A6,-0x234; A4 A3 A2 D3 D2 saved):
 *   A6-0x232  2  bit_temp     the ptrace flags byte as a word
 *   A6-0x22C  2  event_code   what goes into entry+0x94's high byte
 *   A6-0x228  4  susp_status  PROC1_$SUSPEND's status
 *   A6-0x208 F0  aux          FIM_$FP_GET_STATE's second buffer
 *   A6-0x118 100 fp_buf       the FP save area
 *   A6-0x018 11  state        xpd_$debug_state_t (its address goes to
 *                             entry+0xC6)
 *   A2  status_ret; A3 the current entry (+0xE4); A4 scratch
 */
void XPD_$CAPTURE_FAULT(void *context, int32_t *frame_cell, uint16_t *signal,
                        status_$t *status_ret)
{
    uint16_t bit_temp;                  /* A6-0x232 */
    uint16_t event_code;                /* A6-0x22C */
    status_$t susp_status;              /* A6-0x228 */
    uint32_t aux[XPD_AUX_BUF_BYTES / 4];        /* A6-0x208 */
    uint32_t fp_buf[XPD_FP_BUF_BYTES / 4];      /* A6-0x118 */
    xpd_$debug_state_t state;           /* A6-0x18 */
    proc2_info_t *entry;                /* A3 */
    xpd_$ptrace_opts_t *opts;
    status_$t fault;                    /* D0 */
    uint32_t pc;                        /* D2 */
    uint16_t sig;                       /* D1 */
    uint16_t idx;

    /* 0x00E5B1F6-0x00E5B21A: the record: frame = *frame_cell, regs =
     * *context, the two buffers, fp_modified = 0 */
    state.frame = (xpd_$frame_t *)ARCH_VA_TO_PTR(*(uint32_t *)frame_cell);
    state.regs = (uint32_t *)ARCH_VA_TO_PTR(*(uint32_t *)context);
    state.fp_buf = fp_buf;
    state.aux = aux;
    state.fp_modified = 0;

    /* 0x00E5B21E-0x00E5B23E: the current process's entry */
    entry = XPD_ENTRY(XPD_CURRENT_INDEX());
    opts = XPD_PTRACE_OPTS(entry);

    /* 0x00E5B242-0x00E5B292: dispatch on the status */
    fault = *status_ret;
    if (fault == status_$xpd_target_is_forking) {
        /* 0x00E5B296-0x00E5B2A8 */
        event_code = 1;
        if (((uint16_t)opts->flags >> event_code) & 1) {
            *signal = 0;
            goto capture;
        }
        goto ignore;
    } else if (fault == status_$xpd_target_is_vforking) {
        /* 0x00E5B2AA-0x00E5B2B0 -> 0x00E5B318 */
        event_code = 1;
        bit_temp = opts->flags;
        if ((bit_temp >> event_code) & 1) {
            *signal = 1;
            goto capture;
        }
        goto ignore;
    } else if (fault == status_$xpd_target_is_execing) {
        /* 0x00E5B2B2-0x00E5B2BE */
        event_code = 2;
        *signal = 0;
        goto exec;
    } else if (fault == status_$xpd_target_is_invoking) {
        /* 0x00E5B2C0-0x00E5B2CA */
        event_code = 2;
        *signal = 1;
exec:
        /* 0x00E5B2CE-0x00E5B2DE */
        if (xpd_$exec_event(entry, state.frame, state.regs, &event_code,
                            signal) < 0) {
            goto capture;
        }
        goto ignore;
    } else if (fault == status_$xpd_target_is_exiting) {
        /* 0x00E5B2E2 */
        event_code = 4;
        goto flag_test_zero;
    } else if (fault == status_$xpd_target_is_loading_exec_image) {
        /* 0x00E5B2EA */
        event_code = 5;
flag_test_zero:
        /* 0x00E5B2F0-0x00E5B30E */
        bit_temp = opts->flags;
        if ((bit_temp >> event_code) & 1) {
            *signal = 0;
            goto capture;
        }
        goto ignore;
    } else if (fault == status_$xpd_target_is_signalled) {
        /* 0x00E5B312-0x00E5B338 */
        event_code = 5;
        bit_temp = opts->flags;
        if ((bit_temp >> event_code) & 1) {
            *signal = 1;
            goto capture;
        }
        goto ignore;
    } else if (fault == status_$fault_single_step_completed) {
        /* 0x00E5B33C-0x00E5B3C8 */
        pc = XPD_FRAME_PC(state.frame);
        if ((opts->flags & 0x40) != 0) {
            /* trace while inside [lo, hi]: leaving it is event 6 */
            if (pc < opts->trace_range_lo) {
                event_code = 6;
                goto capture;
            }
            if (pc <= opts->trace_range_hi) {
                goto rearm;
            }
            event_code = 6;
            goto capture;
        }
        if ((int8_t)opts->flags < 0) {
            /* trace until inside [lo, hi]: arriving is event 7 */
            if (pc < opts->trace_range_lo) {
                goto rearm;
            }
            if (pc <= opts->trace_range_hi) {
                event_code = 7;
                goto capture;
            }
rearm:
            /* 0x00E5B37C-0x00E5B3A0: another single step */
            FIM_$TRACE_STS[entry->asid] = status_$fault_single_step_completed;
            FIM_$DELIVER_TRACE_FAULT((int16_t)entry->asid);
            XPD_ENTRY_LAST_PC_SET(entry, pc);
            goto ignore;
        }
        /* 0x00E5B3A4-0x00E5B3C8: a plain signal, captured when bit 0 and
         * the mask bit for it are set */
        if ((opts->flags & 0x01) == 0) {
            goto ignore;
        }
        sig = (uint16_t)(*signal - 1);
        if (sig > 0x1F) {
            goto ignore;
        }
        if (((opts->signal_mask >> sig) & 1) == 0) {
            goto ignore;
        }
        event_code = 0;
        goto capture;
    } else if (fault == status_$fault_process_BLAST) {
        /* 0x00E5B288-0x00E5B28E */
        return;
    } else {
        /* 0x00E5B3CA-0x00E5B3EE: any other fault - as above, but a miss
         * returns without touching anything */
        if ((opts->flags & 0x01) == 0) {
            return;
        }
        sig = (uint16_t)(*signal - 1);
        if (sig > 0x1F) {
            return;
        }
        if (((opts->signal_mask >> sig) & 1) == 0) {
            return;
        }
        event_code = 0;
    }

capture:
    /* 0x00E5B3F2-0x00E5B424: save the FP state, publish the record, mark
     * the target suspended, record event code / signal and the status */
    XPD_$FP_GET_STATE(fp_buf, aux);
    XPD_ENTRY_STATE_VA_SET(entry, ARCH_PTR_TO_VA(&state));
    entry->flags |= XPD_PF_SUSPENDED;
    entry->pad_94 = (uint16_t)((event_code << 8) | *signal);
    PROC2_FAULT_PARAM_SET(entry, *status_ret);

    /* 0x00E5B424-0x00E5B448: tell the debugger - through its creation-
     * record eventcount when flags2 bit 4 says so, else the guardian */
    if ((opts->flags2 & 0x10) != 0) {
        EC_$ADVANCE(PROC_CR_REC_EC(entry->debugger_idx));
    } else {
        PROC2_$AWAKEN_GUARDIAN((int16_t *)&entry->self_index);
    }

    /* 0x00E5B484-0x00E5B48A / 0x00E5B456-0x00E5B482: stay suspended until
     * the debugger clears the flag, dropping lock 4 across each suspend */
    while ((entry->flags & XPD_PF_SUSPENDED) != 0) {
        (void)PROC1_$SUSPEND(entry->level1_pid, &susp_status);
        ML_$UNLOCK(PROC2_LOCK_ID);
        ML_$LOCK(PROC2_LOCK_ID);
    }

    /* 0x00E5B48C-0x00E5B4A4 */
    XPD_ENTRY_STATE_VA_SET(entry, 0);
    if (state.fp_modified < 0) {
        XPD_$FP_PUT_STATE(fp_buf, aux);
    }

    /* 0x00E5B4A6-0x00E5B4D8: hand back what the debugger set, remember
     * the PC, drop the state-saved flag and the pending mask */
    *status_ret = (status_$t)PROC2_FAULT_PARAM_GET(entry);
    *signal = (uint16_t)(entry->pad_94 & 0x00FF);
    XPD_ENTRY_LAST_PC_SET(entry, XPD_FRAME_PC(state.frame));
    entry->flags &= (uint16_t)~XPD_PF_STATE_SAVED;
    entry->sig_mask_2 = 0;
    FIM_$CLEAR_TRACE_FAULT((int16_t)entry->asid);

    /* 0x00E5B4DA-0x00E5B502: a requested single step is armed now */
    if ((entry->flags & XPD_PF_TRACE_PENDING) != 0) {
        FIM_$TRACE_STS[entry->asid] = status_$fault_single_step_completed;
        FIM_$DELIVER_TRACE_FAULT((int16_t)entry->asid);
    }

    /* 0x00E5B504-0x00E5B528: a signal the debugger left: if it is in
     * sig_blocked_2 it is queued in sig_mask_2 and swallowed; if it is in
     * sig_blocked_1 it is delivered as is (plain return); otherwise
     * swallowed. */
    if (*signal != 0) {
        idx = (uint16_t)(*signal - 1);
        if ((entry->sig_blocked_2 >> (idx & 31)) & 1) {
            entry->sig_mask_2 |= (uint32_t)1 << (idx & 31);
            goto ignore;
        }
        if ((entry->sig_blocked_1 >> (idx & 31)) & 1) {
            return;
        }
    }

ignore:
    /* 0x00E5B52A-0x00E5B53E */
    FIM_$QUIT_INH[PROC1_$AS_ID] = 0;
    *signal = 0;
}

/*
 * XPD_$GET_EC - the debugger's eventcount, wrapped for EC2
 *
 * *key must be 0 (else invalid_ec_key).  A failing EC2_$REGISTER_EC1
 * status gets bit 31 set (0x00E5BE12 `bset.b #0x7,(A2)`).
 */
void XPD_$GET_EC(int16_t *key, void **ec_ret, status_$t *status_ret)
{
    int16_t slot;                       /* D2 */

    /* 0x00E5BDCE-0x00E5BDE0 */
    slot = XPD_$FIND_DEBUGGER_INDEX((int16_t)PROC1_$AS_ID, status_ret);
    if (slot == 0) {
        return;
    }
    /* 0x00E5BDE2-0x00E5BDE8 */
    if (*key != 0) {
        *status_ret = status_$xpd_invalid_ec_key;
        return;
    }
    /* 0x00E5BDEA-0x00E5BE16 */
    *ec_ret = EC2_$REGISTER_EC1(&XPD_DEBUGGER(slot)->ec, status_ret);
    if (*status_ret != status_$ok) {
        *status_ret = (status_$t)((uint32_t)*status_ret | 0x80000000u);
    }
}

/*
 * XPD_$GET_EVENT_AND_DATA - the first unacknowledged event among the
 * caller's targets
 *
 * Walks target records 1..57 (`moveq #0x38` / `dbf`) for one whose slot is
 * the caller's, not yet ACKED, ENABLED, with a non-zero event code; returns
 * its UID (PROC2_$UID[idx]), event code and status, and marks it ACKED.
 * None (or the caller is no debugger): event 0, UID_$NIL, status 0.
 */
void XPD_$GET_EVENT_AND_DATA(uid_t *proc_uid, uint16_t *event_type,
                             status_$t *status_ret)
{
    status_$t find_status;              /* A6-0x04 */
    int16_t slot;                       /* D0 */
    int16_t idx;                        /* D2 */
    xpd_$target_t *tgt;                 /* A0 */
    uint16_t state;                     /* D3 */

    /* 0x00E5BE38-0x00E5BE4C */
    slot = XPD_$FIND_DEBUGGER_INDEX((int16_t)PROC1_$AS_ID, &find_status);
    if (slot != 0) {
        /* 0x00E5BE4E-0x00E5BEB4 */
        for (idx = 1; idx <= XPD_MAX_TARGETS; idx++) {
            tgt = XPD_TARGET(idx);
            if (((tgt->state & XPD_STATE_DEBUGGER) >> XPD_STATE_DEBUGGER_SHIFT) != slot) {
                continue;
            }
            state = tgt->state;
            if ((state & XPD_STATE_ACKED) != 0) {
                continue;
            }
            if ((state & XPD_STATE_ENABLED) == 0) {
                continue;
            }
            state = (uint16_t)((state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT);
            if (state == 0) {
                continue;
            }
            /* 0x00E5BE84-0x00E5BEAC */
            *proc_uid = PROC2_$UID[idx];
            *event_type = state;
            *status_ret = tgt->status;
            tgt->state |= XPD_STATE_ACKED;
            return;
        }
    }

    /* 0x00E5BEB8-0x00E5BECC */
    *event_type = 0;
    *proc_uid = UID_$NIL;
    *status_ret = 0;
}

/*
 * XPD_$CONTINUE_PROC - answer a target's posted event
 *
 * Stores the LOW byte of *response in state bits 12-13, clears the event
 * code and advances the target's eventcount.  No pending event ->
 * target_not_suspended.  A failing PROC2_$FIND_ASID leaves its status.
 */
void XPD_$CONTINUE_PROC(uid_t *proc_uid, xpd_$response_t *response,
                        status_$t *status_ret)
{
    uint16_t idx;                       /* D0 */
    xpd_$target_t *tgt;                 /* A2 */
    uint8_t resp;                       /* D1 */

    /* 0x00E5BEE4-0x00E5BEFA: the zero cell at 0x00E5BDBE */
    idx = PROC2_$FIND_ASID(proc_uid, &xpd_$find_asid_flag, status_ret);
    if (idx == 0) {
        return;
    }
    /* 0x00E5BEFC-0x00E5BF22 */
    tgt = XPD_TARGET(idx);
    if (((tgt->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT) == 0) {
        *status_ret = status_$xpd_target_not_suspended;
        return;
    }
    /* 0x00E5BF24-0x00E5BF40: `andi.b #0xcf` / `or.b resp<<4` on the high
     * byte, `andi.w #0xfe1f` on the word */
    resp = (uint8_t)(*response & 0x00FF);
    tgt->state = (uint16_t)((tgt->state & ~XPD_STATE_RESPONSE) |
                            (uint16_t)(((resp << 4) & 0xF0) << 8));
    tgt->state &= (uint16_t)~XPD_STATE_EVENT;
    EC_$ADVANCE(&tgt->ec);
}

/*
 * XPD_$SET_ENABLE - turn event capture for a target on or off
 *
 * Under lock 2: the sign bit of *enable becomes XPD_STATE_ENABLED.  Enabling
 * also discards any pending event code; disabling a target with a pending
 * event continues it with response 2 (the 0x00E5BDC0 cell).  A5 is set to
 * 0x00E81814 and never used.
 */
void XPD_$SET_ENABLE(uid_t *proc_uid, int8_t *enable, status_$t *status_ret)
{
    uint16_t idx;                       /* D2 */
    xpd_$target_t *tgt;                 /* A0 */

    /* 0x00E5BF66-0x00E5BF88 */
    idx = PROC2_$FIND_ASID(proc_uid, &xpd_$find_asid_flag, status_ret);
    ML_$LOCK(XPD_LOCK_ID);
    if (idx != 0) {
        /* 0x00E5BF8E-0x00E5BFAE */
        tgt = XPD_TARGET(idx);
        tgt->state = (uint16_t)((tgt->state & 0x7FFF) |
                                (uint16_t)((*enable & 0x80) << 8));
        /* 0x00E5BFB2-0x00E5BFE2 */
        if (*enable < 0) {
            tgt->state &= (uint16_t)~XPD_STATE_EVENT;
        } else if (((tgt->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT) != 0) {
            XPD_$CONTINUE_PROC(&PROC2_$UID[idx], &xpd_$response_two, status_ret);
        }
    }
    /* 0x00E5BFE6-0x00E5BFF2 */
    ML_$UNLOCK(XPD_LOCK_ID);
}
