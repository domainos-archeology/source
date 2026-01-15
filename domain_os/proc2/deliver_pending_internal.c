/*
 * PROC2_$DELIVER_PENDING_INTERNAL - Deliver pending signals to process
 *
 * Finds the next pending signal and delivers it via FIM.
 * Called when a process has pending signals and is not suspended.
 *
 * Parameters:
 *   proc_index - Index in process table
 *
 * Original address: 0x00e3ecea
 */

#include "proc2/proc2_internal.h"

/* FIM globals for signal delivery */
#if defined(M68K)
    #define FIM_QUIT_INH_TABLE      ((uint8_t*)0xE2248A)
    #define FIM_TRACE_STS_TABLE     ((uint32_t*)0xE223A2)
    #define FIM_QUIT_EC_BASE        0xE22002
#else
    #define FIM_QUIT_INH_TABLE      fim_quit_inh_table
    #define FIM_TRACE_STS_TABLE     fim_trace_sts_table
    #define FIM_QUIT_EC_BASE        ((uintptr_t)fim_quit_ec_base)
#endif

/* Compute FIM quit EC address for ASID */
#define FIM_QUIT_EC(asid)       ((ec_$eventcount_t*)(FIM_QUIT_EC_BASE + (asid) * 0x0C))

/* Special parameter value indicating SIGCONT from wait */
#define SIGCONT_FROM_WAIT       0x00120019

/* Signal mask for stoppable signals */
#define STOPPABLE_SIGNAL_MASK   0xFE67FFFF

/* Fault parameter storage offset in proc2_info_t */
#define FAULT_PARAM_OFFSET      0x90

/*
 * PROC2_$GET_NEXT_PENDING_SIGNAL - Get next deliverable signal
 *
 * Scans pending signals to find one that should be delivered.
 * Handles special case for SIGCONT from wait.
 *
 * Parameters:
 *   entry - Pointer to process entry
 *
 * Returns:
 *   Signal number (1-32), or 0 if none pending
 *
 * Original address: 0x00e3ef38
 */
static int16_t get_next_pending_signal(proc2_info_t *entry)
{
    uint32_t pending;
    int16_t sig;

    /*
     * Check for pending SIGCONT (19) from wait.
     * This has priority and should be delivered first.
     */
    if ((entry->sig_mask_2 & 0x40000) != 0) {
        int32_t fault_param = *(int32_t*)((char*)entry + FAULT_PARAM_OFFSET);
        if (fault_param == SIGCONT_FROM_WAIT) {
            return SIGCONT;  /* 19 */
        }
    }

    /*
     * Get pending signals that are not blocked.
     * pending = sig_mask_2 & ~sig_blocked_2
     */
    pending = entry->sig_mask_2 & ~entry->sig_blocked_2;

    /*
     * If process has ALT_ASID flag (vfork), mask out stoppable signals.
     */
    if ((entry->flags & PROC2_FLAG_ALT_ASID) != 0) {
        pending &= STOPPABLE_SIGNAL_MASK;
    }

    if (pending == 0) {
        return 0;
    }

    /*
     * Find first set bit (lowest signal number).
     * Signals are 1-based, so we return bit_position + 1.
     */
    for (sig = 0; sig < 32; sig++) {
        if ((pending & (1U << sig)) != 0) {
            return sig + 1;
        }
    }

    return 0;
}

void PROC2_$DELIVER_PENDING_INTERNAL(int16_t proc_index)
{
    proc2_info_t *entry;
    int16_t signal;
    uint16_t asid;
    int16_t current_idx;
    int32_t fault_param;

    entry = P2_INFO_ENTRY(proc_index);

    /* Get next pending signal */
    signal = get_next_pending_signal(entry);

    if (signal == 0) {
        return;  /* No pending signals */
    }

    asid = entry->asid;

    /*
     * Check if we can deliver the signal.
     * Delivery is allowed if:
     * 1. FIM_QUIT_INH[asid] == 0 (no inhibit), OR
     * 2. Signal is SIGKILL and target is current process's debugger, OR
     * 3. Signal is SIGCONT with SIGCONT_FROM_WAIT parameter
     */
    if (FIM_QUIT_INH_TABLE[asid] != 0) {
        /* Inhibited - check exceptions */

        if (signal == SIGKILL) {
            /* SIGKILL: check if target is our debugger */
            current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
            if (entry->debugger_idx != current_idx) {
                /* Not our debugger, can't deliver */
                goto check_sigcont;
            }
            /* Fall through to deliver SIGKILL to debugger */
        } else {
check_sigcont:
            if (signal != SIGCONT) {
                return;  /* Can't deliver */
            }
            fault_param = *(int32_t*)((char*)entry + FAULT_PARAM_OFFSET);
            if (fault_param != SIGCONT_FROM_WAIT) {
                return;  /* Can't deliver */
            }
            /* Fall through to deliver SIGCONT from wait */
        }
    }

    /*
     * Set up FIM trace status for signal delivery.
     * For SIGCONT (19), store the fault parameter.
     * For other signals, clear the trace status.
     */
    if (signal == SIGCONT) {
        fault_param = *(int32_t*)((char*)entry + FAULT_PARAM_OFFSET);
        FIM_TRACE_STS_TABLE[asid] = (uint32_t)fault_param;
    } else {
        FIM_TRACE_STS_TABLE[asid] = 0;
    }

    /* Set high bit of trace status to indicate signal delivery */
    FIM_TRACE_STS_TABLE[asid] |= 0x80;

    /* Set FIM quit inhibit to prevent re-entry */
    FIM_QUIT_INH_TABLE[asid] = 0xFF;

    /* Deliver the fault/signal via FIM */
    FIM_$DELIVER_TRACE_FAULT(asid);

    /* Advance the quit eventcount to wake waiters */
    EC_$ADVANCE(FIM_QUIT_EC(asid));
}
