/*
 * PROC2_$SIGNAL_PGROUP_OS - Send signal to process group (OS internal)
 *
 * Sends a signal to all processes in a process group without permission checking.
 * Used internally by the OS when it needs to signal a process group.
 *
 * Parameters:
 *   pgroup_uid - Pointer to process group UID
 *   signal     - Pointer to signal number
 *   param      - Pointer to signal parameter
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e3f2c2
 */

#include "proc2.h"

void PROC2_$SIGNAL_PGROUP_OS(uid_t *pgroup_uid, int16_t *signal, uint32_t *param,
                             status_$t *status_ret)
{
    int16_t pgroup_idx;
    status_$t status;
    uid_t uid_copy;
    uint32_t param_copy;
    int16_t sig_copy;

    /* Copy inputs before locking */
    uid_copy = *pgroup_uid;
    sig_copy = *signal;
    param_copy = *param;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Convert pgroup UID to index */
    pgroup_idx = PROC2_$UID_TO_PGROUP_INDEX(&uid_copy);

    /* Signal all processes in group without permission checking (0) */
    PROC2_$SIGNAL_PGROUP_INTERNAL(pgroup_idx, sig_copy, param_copy, 0, &status);

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}
