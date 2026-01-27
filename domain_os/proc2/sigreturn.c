/*
 * PROC2_$SIGRETURN - Return from signal handler
 *
 * Called to return from a signal handler. Restores the signal mask
 * from the sigcontext, checks for pending signals that can now be
 * delivered, populates the result array, and transfers control to
 * FIM_$FAULT_RETURN which restores user-mode state via RTE.
 *
 * Parameters:
 *   context_ptr  - Pointer to pointer to sigcontext_t
 *   regs_ptr     - Pointer to pointer to register save area
 *   fp_state_ptr - Pointer to FP state
 *   result       - Output: result[0] = blocked mask, result[1] = flag
 *
 * Does not return.
 *
 * Original address: 0x00e3f582
 */

#include "proc2/proc2_internal.h"

/* Flag bits in flags field */
#define FLAG_ONSTACK    0x0004  /* Bit 2: Signal handler on signal stack */
#define FLAG_BIT_10     0x0400  /* Bit 10: Some signal flag */

NORETURN void PROC2_$SIGRETURN(void *context_ptr, void *regs_ptr,
                                void *fp_state_ptr, uint32_t *result)
{
    sigcontext_t **ctx_pp = (sigcontext_t **)context_ptr;
    sigcontext_t *sigctx;
    int32_t onstack;
    uint32_t new_mask;
    int16_t current_idx;
    proc2_info_t *entry;

    /* Get signal context from the double-indirect context pointer */
    sigctx = *ctx_pp;
    onstack = sigctx->sc_onstack;
    new_mask = sigctx->sc_mask;

    /* Get current process entry */
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
    entry = P2_INFO_ENTRY(current_idx);

    ML_$LOCK(PROC2_LOCK_ID);

    /*
     * Update onstack flag in process flags.
     * Clear bit 2, then set it if onstack is non-zero.
     */
    entry->flags &= ~FLAG_ONSTACK;
    if (onstack != 0) {
        entry->flags |= FLAG_ONSTACK;
    }

    /* Restore signal mask */
    entry->sig_blocked_2 = new_mask;

    /*
     * Check if any signals became deliverable.
     * If (pending & ~blocked) != 0, deliver them.
     */
    if ((entry->sig_mask_2 & ~entry->sig_blocked_2) != 0) {
        PROC2_$DELIVER_PENDING_INTERNAL(entry->owner_session);
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    /*
     * Populate result for the caller (FIM trampoline):
     *   result[0] = current blocked mask
     *   result[1] = 1 if FLAG_BIT_10 set, else 0
     */
    result[0] = entry->sig_blocked_2;
    result[1] = (entry->flags & FLAG_BIT_10) ? 1 : 0;

    /* Return to interrupted context via FIM (does not return) */
    FIM_$FAULT_RETURN((sigcontext_t **)context_ptr,
                      (uint32_t **)regs_ptr,
                      fp_state_ptr);
}
