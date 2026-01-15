/*
 * PROC2_$SET_SERVER - Set server flag for process
 *
 * If server_flag's high bit (0x80) is set, marks the process as a server.
 * Modifies bit 1 (PROC2_FLAG_SERVER) of the process flags field.
 *
 * Parameters:
 *   proc_uid    - Pointer to process UID
 *   server_flag - Pointer to flag byte; if bit 7 set, process becomes server
 *   status_ret  - Returns status (0 on success)
 *
 * Original address: 0x00e41468
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_SERVER(uid_t *proc_uid, int8_t *server_flag, status_$t *status_ret)
{
    int8_t flag_val;
    int16_t index;
    proc2_info_t *info;
    status_$t status;

    /* Read the flag value before locking */
    flag_val = *server_flag;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if (status == 0) {
        info = P2_INFO_ENTRY(index);

        /* Clear the server bit */
        info->flags &= ~PROC2_FLAG_SERVER;

        /* Set server bit if input flag's high bit is set */
        /* (flag_val >> 7) gives -1 if bit 7 set (signed), 0 otherwise */
        /* Multiply by -2 gives 2 (0x02) if set, 0 otherwise */
        if (flag_val < 0) {
            info->flags |= PROC2_FLAG_SERVER;
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}
