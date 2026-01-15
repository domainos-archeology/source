/*
 * PROC2_$GET_PID - Get PROC1 PID from UID
 *
 * Looks up the process by UID and returns its PROC1 (level1) PID.
 *
 * Parameters:
 *   proc_uid - UID of process
 *   status_ret - Status return
 *
 * Returns: PROC1 PID (level1_pid field from proc2_info_t)
 *
 * Original address: 0x00e40cba
 */

#include "proc2/proc2_internal.h"

uint16_t PROC2_$GET_PID(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;
    uint16_t pid;
    status_$t status;
    proc2_info_t *entry;

    pid = 0;  /* Default return if not found */

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if (status == 0) {
        entry = P2_INFO_ENTRY(index);
        pid = entry->level1_pid;
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
    return pid;
}
