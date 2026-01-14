/*
 * PROC2_$SIGRETURN - Return from signal handler
 *
 * Called to return from a signal handler. Restores the signal mask
 * and checks for pending signals that can now be delivered.
 *
 * The context parameter points to saved state from the signal handler:
 * - [0] = onstack flag (non-zero if handler was on signal stack)
 * - [4] = signal mask to restore
 *
 * Parameters:
 *   context - Pointer to signal context (contains onstack flag and mask)
 *
 * Original address: 0x00e3f582
 */

#include "proc2.h"

/* Flag bits in flags field */
#define FLAG_ONSTACK    0x0004  /* Bit 2: Signal handler on signal stack */
#define FLAG_BIT_10     0x0400  /* Bit 10: Some signal flag */

/* External function */
extern void FIM_$FAULT_RETURN(void *context, void *param2, void *param3);

/*
 * Signal context structure passed from signal handler
 */
typedef struct sigcontext_t {
    int32_t     onstack;        /* Non-zero if on signal stack */
    uint32_t    sig_mask;       /* Signal mask to restore */
} sigcontext_t;

void PROC2_$SIGRETURN(void *context)
{
    int32_t **context_ptr = (int32_t**)context;
    sigcontext_t *sigctx;
    int32_t onstack;
    uint32_t new_mask;
    int16_t current_idx;
    proc2_info_t *entry;

    /* Get signal context from the double-indirect context pointer */
    sigctx = (sigcontext_t*)*context_ptr;
    onstack = sigctx->onstack;
    new_mask = sigctx->sig_mask;

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
     * The original code also sets output values in param_4:
     * - param_4[0] = current blocked mask
     * - param_4[1] = 1 if FLAG_BIT_10 set, else 0
     *
     * This appears to be for internal use by FIM_$FAULT_RETURN.
     * Since our API only has the context parameter, this is handled
     * internally by the register-based calling convention.
     */

    /* Return to interrupted context via FIM */
    /* Note: FIM_$FAULT_RETURN does not return */
    FIM_$FAULT_RETURN(context, NULL, NULL);
}
