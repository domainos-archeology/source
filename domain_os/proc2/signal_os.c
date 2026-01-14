/*
 * PROC2_$SIGNAL_OS - Send signal to process (OS internal, no permission check)
 *
 * Sends a signal to a process without checking permissions.
 * Used internally by the OS when it needs to signal a process.
 *
 * Parameters:
 *   proc_uid   - Pointer to target process UID
 *   signal     - Pointer to signal number
 *   param      - Pointer to signal parameter
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e3f0a6
 */

#include "proc2.h"

/* Forward declaration of internal function */
extern void PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t index, int16_t signal,
                                           uint32_t param, status_$t *status_ret);

/* Log signal event (debugging) - currently a no-op */
static void log_signal_event(int event_type, int16_t target_idx, int16_t signal,
                            uint32_t param, status_$t status)
{
    /* TODO: Implement signal event logging if needed */
    (void)event_type;
    (void)target_idx;
    (void)signal;
    (void)param;
    (void)status;
}

void PROC2_$SIGNAL_OS(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                      status_$t *status_ret)
{
    int16_t index;
    status_$t status;
    uint32_t param_copy;

    /* Copy parameter before locking */
    param_copy = *param;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if (status == 0) {
        /* Deliver the signal without permission check */
        PROC2_$DELIVER_SIGNAL_INTERNAL(index, *signal, param_copy, &status);
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;

    /* Log the signal event */
    log_signal_event(1, index, *signal, *param, status);
}
