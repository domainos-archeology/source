/*
 * PROC2_$SIGNAL_PGROUP - Send signal to all processes in a process group
 *
 * Sends a signal to all processes in a process group with permission checking.
 * Each process in the group is signaled independently, with ACL checks.
 *
 * Parameters:
 *   pgroup_uid - Pointer to process group UID
 *   signal     - Pointer to signal number
 *   param      - Pointer to signal parameter
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e3f23e
 */

#include "proc2.h"

/* Forward declaration of internal helper */
void PROC2_$SIGNAL_PGROUP_INTERNAL(int16_t pgroup_idx, int16_t signal,
                                    uint32_t param, int8_t check_perms,
                                    status_$t *status_ret);

void PROC2_$SIGNAL_PGROUP(uid_t *pgroup_uid, int16_t *signal, uint32_t *param,
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

    /* Signal all processes in group with permission checking enabled (0xFF) */
    PROC2_$SIGNAL_PGROUP_INTERNAL(pgroup_idx, sig_copy, param_copy, -1, &status);

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}
