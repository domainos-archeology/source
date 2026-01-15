/*
 * PROC2_$DELIVER_SIGNAL_INTERNAL - Internal signal delivery
 *
 * Core internal function for delivering signals to a process. Handles:
 * - Signal mask checking
 * - SIGKILL/SIGCONT special cases (cannot be blocked)
 * - Fault signals
 * - Setting pending signal bits
 * - Waking suspended processes
 *
 * Parameters:
 *   proc_index - Index in process table
 *   signal     - Signal number (1-31)
 *   param      - Signal parameter (e.g., fault code, or 0x120019 for SIGCONT from wait)
 *   status_ret - Pointer to receive status
 *
 * Returns:
 *   Result from DELIVER_PENDING_INTERNAL or PROC1_$RESUME
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$proc2_another_fault_pending - Fault signal when one already pending
 *
 * Original address: 0x00e3eb8c
 */

#include "proc2/proc2_internal.h"

/* Special parameter value indicating SIGCONT from wait */
#define SIGCONT_FROM_WAIT       0x00120019

/* Signal mask for "stoppable" signals (can be blocked for job control) */
#define STOPPABLE_SIGNAL_MASK   0xFE67FFFF  /* ~(SIGSTOP | SIGTSTP | SIGTTIN | SIGTTOU) */

/* Signal mask for signals that cannot trigger pending delivery */
#define NO_PENDING_MASK         0x3D9DFFFF

/* Process flag bits in flags field (offset 0x2A-0x2B) */
#define FLAG_SUSPENDED          0x4000  /* Bit 6 of high byte (0x2B): Process is suspended */
#define FLAG_FAULT_MODE         0x1000  /* Bit 4 of high byte (0x2B): Process in fault mode */
#define FLAG_SIGHUP_PENDING     0x0002  /* Bit 1 of low byte (0x2A): SIGHUP pending */

/* Additional fields in proc2_info_t not in main header */
/* These are stored in the pad areas */
#define FAULT_PARAM_OFFSET      0x90    /* Fault parameter storage */
#define FAULT_SIGNAL_OFFSET     0xC2    /* Stored fault signal info */
#define FAULT_FLAG_OFFSET       0xC3    /* Fault flag byte */
#define PENDING_SIGNAL_OFFSET   0x94    /* Pending signal number */

uint32_t PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t proc_index, int16_t signal,
                                         int32_t param, status_$t *status_ret)
{
    uint32_t sig_mask;
    proc2_info_t *entry;

    *status_ret = status_$ok;

    /* Compute signal mask bit (signal numbers are 1-based) */
    sig_mask = 1U << ((signal - 1) & 0x1F);

    entry = P2_INFO_ENTRY(proc_index);

    /*
     * Check if process is suspended (flag bit 6 of high byte).
     * Certain signals must wake the process:
     * - SIGKILL (9) - Always wakes
     * - SIGCONT (22) - Always wakes (note: Domain/OS uses 22 for SIGCONT, not BSD's 19)
     * - SIGCONT (19) with SIGCONT_FROM_WAIT param - Wakes from wait
     */
    if ((entry->flags & FLAG_SUSPENDED) != 0) {
        int should_wake = 0;

        if (signal == SIGKILL || signal == 22) {  /* SIGKILL or SIGCONT */
            should_wake = 1;
        } else if (signal == SIGCONT && param == SIGCONT_FROM_WAIT) {
            should_wake = 1;
        }

        if (should_wake) {
            /* Clear suspended flag and resume process */
            entry->flags &= ~FLAG_SUSPENDED;
            PROC1_$RESUME(entry->level1_pid, status_ret);
        }
    }

    /*
     * Check if process is in fault mode (flag bit 4 of high byte).
     * Only SIGKILL or SIGCONT with SIGCONT_FROM_WAIT can interrupt fault handling.
     */
    if ((entry->flags & FLAG_FAULT_MODE) != 0) {
        if (signal == SIGKILL || (signal == SIGCONT && param == SIGCONT_FROM_WAIT)) {
            /* Store fault info and resume */
            *(int32_t*)((char*)entry + FAULT_SIGNAL_OFFSET) = param;
            *(uint8_t*)((char*)entry + FAULT_FLAG_OFFSET) |= 0x80;
            *(int16_t*)((char*)entry + PENDING_SIGNAL_OFFSET) = signal;

            /* Clear fault mode flag */
            entry->flags &= ~FLAG_FAULT_MODE;

            /* Resume the process */
            PROC1_$RESUME(entry->level1_pid, status_ret);
            return 0;
        }
    }

    /*
     * For SIGCONT (22), clear certain pending signal bits.
     * This clears stop signals (SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU).
     */
    if (signal == 22) {  /* SIGCONT */
        entry->sig_mask_2 &= STOPPABLE_SIGNAL_MASK;
    }

    /*
     * Check if signal is blocked by sig_blocked_1.
     * If blocked and not special, handle SIGHUP specially or ignore.
     */
    if ((sig_mask & ~entry->sig_blocked_1) == 0) {
        /* Signal is blocked */
        if (signal == SIGHUP) {
            /* Mark SIGHUP pending in flags */
            entry->flags |= FLAG_SIGHUP_PENDING;
        }
        /* Check if process has debugger - if so, continue processing */
        if (entry->debugger_idx == 0) {
            return 0;
        }
    }

    /*
     * Check if this is a "stoppable" signal that should be ignored
     * when process is suspended.
     */
    if ((sig_mask & STOPPABLE_SIGNAL_MASK) == 0) {
        /* Clear a flag bit and check if suspended */
        *(uint8_t*)((char*)entry + 0x81) &= ~0x20;  /* sig_mask_2 high byte */
        if ((entry->flags & FLAG_SUSPENDED) != 0) {
            return 0;
        }
    }

    /*
     * Check various signal masks to determine if delivery should proceed.
     */
    if ((sig_mask & ~entry->sig_pending) == 0) {
        /* Signal already pending */
        goto check_pending_delivery;
    }
    if ((sig_mask & ~entry->sig_blocked_2) == 0) {
        /* Signal blocked by second mask */
        goto check_pending_delivery;
    }
    if ((sig_mask & NO_PENDING_MASK) == 0) {
        /* Signal in no-pending set */
        return 0;
    }

check_pending_delivery:
    /*
     * For SIGCONT (19), check if another fault is already pending.
     */
    if (signal == SIGCONT) {
        if ((~entry->sig_mask_2 & 0x40000) == 0) {
            /* Fault signal bit not set - no conflict */
            if (param != SIGCONT_FROM_WAIT) {
                *status_ret = status_$proc2_another_fault_pending;
                return 0;
            }
        }
        /* Store fault parameter */
        *(int32_t*)((char*)entry + FAULT_PARAM_OFFSET) = param;
    }

    /* Set the pending signal bit */
    entry->sig_mask_2 |= sig_mask;

    /* If process is not suspended, deliver pending signals */
    if ((entry->flags & FLAG_SUSPENDED) == 0) {
        PROC2_$DELIVER_PENDING_INTERNAL(proc_index);
    }

    return 0;
}
