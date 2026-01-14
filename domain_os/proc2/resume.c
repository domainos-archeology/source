/*
 * PROC2_$RESUME - Resume a suspended process
 *
 * Resumes a process that was previously suspended. Translates
 * PROC1 status codes to PROC2 equivalents.
 *
 * Parameters:
 *   proc_uid   - Pointer to process UID to resume
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e413da
 */

#include "proc2.h"

void PROC2_$RESUME(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;
    proc2_info_t *info;
    status_$t status;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if ((status >> 16) == 0) {  /* Check high word is 0 */
        info = P2_INFO_ENTRY(index);

        /* Call PROC1 to resume the process */
        PROC1_$RESUME(info->level1_pid, &status);

        /* Translate PROC1 status codes to PROC2 equivalents */
        if ((status >> 16) != 0) {
            if (status == status_$process_not_suspended) {
                status = status_$proc2_not_suspended;
            } else {
                /* Set high bit to indicate PROC1 error */
                status |= 0x80000000;
            }
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}
