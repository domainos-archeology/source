/*
 * PROC2_$DELIVER_PENDING_INTERNAL - Deliver pending signals to process
 *
 * Picks the next deliverable signal for a process table entry and, unless
 * quit delivery is inhibited for its address space (with two exceptions),
 * arms FIM's trace-fault machinery for it.
 *
 * Parameters:
 *   proc_index - (0x8,A6) word: process table index
 *
 * Original address: 0x00e3ecea (214 bytes)
 * A2 = 0xEA551C + proc_index*0xE4 = entry + 0xE4, so
 *   (-0xE4,A2) = entry      (-0x4E,A2) = +0x96 asid
 *   (-0xBE,A2) = +0x26 debugger_idx   (-0x54,A2) = +0x90 sig_status
 *
 * Also holds PROC2_$GET_NEXT_PENDING_SIGNAL (0x00E3EF38), the module-local
 * helper both this routine and PROC2_$DELIVER_FIM call.
 */

#include "proc2/proc2_internal.h"

/* 0x00E3EF72: andi.l #-0x1980001 */
#define P2_SIG_STOP_CLEAR_MASK   0xFE67FFFFUL

/*
 * PROC2_$GET_NEXT_PENDING_SIGNAL - Get next deliverable signal
 *
 * Returns 0x13 at once when it is pending with the BLAST status; otherwise
 * the lowest pending, unblocked signal number (1-based), or 0.
 *
 * Parameters:
 *   entry - (0x8,A6) pointer to the process entry (no +0xE4 bias here)
 *
 * Original address: 0x00e3ef38 (104 bytes)
 */
int16_t PROC2_$GET_NEXT_PENDING_SIGNAL(proc2_info_t *entry)
{
    uint16_t sig;        /* D0w */
    uint32_t pending;    /* D1 */
    int16_t count;       /* D2w, dbf counter */

    /* 0x00E3EF44 */
    sig = 0;

    /* 0x00E3EF46..0x00E3EF5C: btst.l #0x12 of +0x80, then +0x90 == BLAST */
    pending = entry->sig_mask_2;
    if ((pending & 0x00040000UL) != 0 &&
        entry->sig_status == (uint32_t)status_$fault_process_BLAST) {
        return 0x13;
    }

    /* 0x00E3EF5E..0x00E3EF66: pending = ~(+0x78) & (+0x80) */
    pending = ~entry->sig_blocked_2 & entry->sig_mask_2;

    /* 0x00E3EF68..0x00E3EF72: flags bit 11 (0x0800) masks the stop signals */
    if ((entry->flags & 0x0800) != 0) {
        pending &= P2_SIG_STOP_CLEAR_MASK;
    }

    /* 0x00E3EF78 */
    if (pending == 0) {
        return 0;
    }

    /* 0x00E3EF7C..0x00E3EF8C: moveq #0x1f,D2 / dbf = up to 32 probes.  The
     * `cmp.w D0w,D3w / bcs` guard (skip the btst when 31 < D0) can never
     * fire inside the loop but is reproduced. */
    count = 31;
    do {
        if (!(31U < sig) && ((pending >> sig) & 1U) != 0) {
            break;
        }
        sig++;
    } while (count-- != 0);

    /* 0x00E3EF90..0x00E3EF94 */
    return (int16_t)(sig + 1);
}

void PROC2_$DELIVER_PENDING_INTERNAL(int16_t proc_index)
{
    proc2_info_t *entry;
    int16_t signal;        /* D0w */
    uint16_t asid;         /* D1w */

    entry = P2_INFO_ENTRY(proc_index);

    /* 0x00E3ED06..0x00E3ED12 */
    signal = PROC2_$GET_NEXT_PENDING_SIGNAL(entry);
    if (signal == 0) {
        return;
    }

    asid = entry->asid;

    /* 0x00E3ED16..0x00E3ED56: FIM_$QUIT_INH[asid] != 0 blocks delivery,
     * except for signal 9 sent by the target's debugger (the current
     * process) and for 0x13 carrying the BLAST status */
    if (FIM_$QUIT_INH[asid] != 0) {
        if (signal == 9 &&
            entry->debugger_idx == P2_PID_TO_INDEX(PROC1_$CURRENT)) {
            /* 0x00E3ED46 beq -> deliver */
        } else if (signal == 0x13 &&
                   entry->sig_status == (uint32_t)status_$fault_process_BLAST) {
            /* 0x00E3ED56 falls through to deliver */
        } else {
            return;
        }
    }

    /* 0x00E3ED58..0x00E3ED76: FIM_$TRACE_STS[asid] = sig_status or 0 */
    if (signal == 0x13) {
        FIM_$TRACE_STS[asid] = (status_$t)entry->sig_status;
    } else {
        FIM_$TRACE_STS[asid] = 0;
    }

    /* 0x00E3ED7A..0x00E3ED80 ori.w #0x80,(0x0,A1,D1w): the HIGH word of the
     * longword, i.e. bit 23 */
    FIM_$TRACE_STS[asid] |= 0x00800000L;

    /* 0x00E3ED86..0x00E3ED8A st FIM_$QUIT_INH[asid] */
    FIM_$QUIT_INH[asid] = (int8_t)0xFF;

    /* 0x00E3ED8E..0x00E3ED98: result slot + word argument */
    FIM_$DELIVER_TRACE_FAULT((int16_t)asid);

    /* 0x00E3ED9A..0x00E3EDB0: pea FIM_$QUIT_EC + asid*12 */
    EC_$ADVANCE(&FIM_$QUIT_EC[asid]);
}
