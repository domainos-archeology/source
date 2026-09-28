/*
 * xpd/ptrace_opts.c - the per-process ptrace option record
 *
 *   XPD_$SET_PTRACE_OPTS         0x00E5AF9E  216 bytes
 *   XPD_$INQ_PTRACE_OPTS         0x00E5B076  224 bytes
 *   XPD_$RESET_PTRACE_OPTS       0x00E5B156   30 bytes
 *   XPD_$INHERIT_PTRACE_OPTIONS  0x00E5B174   20 bytes
 *
 * The record lives at proc2_info_t +0xCE (`lea (-0x16,A0)`).  A NIL uid
 * means the current process; otherwise the caller must be the process
 * itself (+0x1C) or its debugger (+0x26), else proc_not_debug_target.
 */

#include "xpd/xpd_internal.h"

/*
 * XPD_$SET_PTRACE_OPTS
 *
 * Frame (link.w A6,-0x24; A2 saved): A6-0x1C status, A6-0x18 uid copy,
 * A6-0x10 the 14-byte copy of *opts (three longwords and a word).
 */
void XPD_$SET_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts,
                          status_$t *status_ret)
{
    status_$t st;                       /* A6-0x1C */
    uid_t uid;                          /* A6-0x18 */
    xpd_$ptrace_opts_t local;           /* A6-0x10 */
    int16_t idx;                        /* D0 */
    uint16_t current;                   /* D1 */
    proc2_info_t *entry;                /* A0 */

    /* 0x00E5AFA4-0x00E5AFD0 */
    uid = *proc_uid;
    local = *opts;
    st = status_$ok;
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E5AFD2-0x00E5B00C: NIL -> the current index, else look it up */
    if (uid.high == UID_$NIL.high && uid.low == UID_$NIL.low) {
        idx = (int16_t)XPD_CURRENT_INDEX();
    } else {
        idx = PROC2_$FIND_INDEX(&uid, &st);
    }

    /* 0x00E5B00E-0x00E5B058 */
    if (st == status_$ok) {
        entry = XPD_ENTRY(idx);
        current = XPD_CURRENT_INDEX();
        if (current == entry->debugger_idx || current == entry->self_index) {
            *XPD_PTRACE_OPTS(entry) = local;
        } else {
            st = status_$proc2_proc_not_debug_target;
        }
    }

    /* 0x00E5B05A-0x00E5B06A */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = st;
}

/*
 * XPD_$INQ_PTRACE_OPTS
 *
 * The same frame; the record is copied out to A6-0x10 under the lock and
 * to *opts only after a good status (0x00E5B13A `bne`).  The two identity
 * tests are made in the other order (self first).
 */
void XPD_$INQ_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts,
                          status_$t *status_ret)
{
    status_$t st;                       /* A6-0x1C */
    uid_t uid;                          /* A6-0x18 */
    xpd_$ptrace_opts_t local;           /* A6-0x10 */
    int16_t idx;                        /* D0 */
    uint16_t current;                   /* D1 */
    proc2_info_t *entry;                /* A0 */

    /* 0x00E5B082-0x00E5B09E */
    uid = *proc_uid;
    st = status_$ok;
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E5B0A0-0x00E5B0DA */
    if (uid.high == UID_$NIL.high && uid.low == UID_$NIL.low) {
        idx = (int16_t)XPD_CURRENT_INDEX();
    } else {
        idx = PROC2_$FIND_INDEX(&uid, &st);
    }

    /* 0x00E5B0DC-0x00E5B126 */
    if (st == status_$ok) {
        entry = XPD_ENTRY(idx);
        current = XPD_CURRENT_INDEX();
        if (current == entry->self_index || current == entry->debugger_idx) {
            local = *XPD_PTRACE_OPTS(entry);
        } else {
            st = status_$proc2_proc_not_debug_target;
        }
    }

    /* 0x00E5B128-0x00E5B14A */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = st;
    if (st == status_$ok) {
        *opts = local;
    }
}

/*
 * XPD_$RESET_PTRACE_OPTS - clear a record (in the image's field order)
 */
void XPD_$RESET_PTRACE_OPTS(xpd_$ptrace_opts_t *opts)
{
    /* 0x00E5B15E-0x00E5B16C */
    opts->flags = 0;
    opts->signal_mask = 0;
    opts->flags2 = 0;
    opts->trace_range_lo = 0;
    opts->trace_range_hi = 0;
}

/*
 * XPD_$INHERIT_PTRACE_OPTIONS - flags2 bit 3, as a Domain boolean (`sne`)
 */
int8_t XPD_$INHERIT_PTRACE_OPTIONS(xpd_$ptrace_opts_t *opts)
{
    /* 0x00E5B17C-0x00E5B182 */
    return ((opts->flags2 & 0x08) != 0) ? -1 : 0;
}
