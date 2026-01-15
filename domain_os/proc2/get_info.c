/*
 * PROC2_$GET_INFO - Get process info by UID
 *
 * Returns process information for a process identified by its UID.
 *
 * Parameters:
 *   proc_uid   - Pointer to process UID
 *   info       - Buffer to receive info (up to 0xE4 bytes)
 *   info_len   - Pointer to buffer length (capped at 0xE4)
 *   status_ret - Pointer to receive status
 *
 * Original address: 0x00e4086e
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_INFO(uid_t *proc_uid, void *info, uint16_t *info_len, status_$t *status_ret)
{
    uid_t uid;
    uint16_t len;
    int16_t proc2_idx;
    int16_t proc1_pid;
    proc2_info_t *entry;
    uint8_t local_info[0xE4];
    status_$t status;

    len = *info_len;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Find process by UID */
    proc2_idx = PROC2_$FIND_INDEX(&uid, &status);

    if ((status & 0xFFFF) == 0 || status == status_$proc2_zombie) {
        /* Process found (or is zombie) */
        if (status == status_$proc2_zombie) {
            /* Zombie - no PROC1 info */
            proc1_pid = 0;
        } else {
            /* Get PROC1 PID from entry */
            entry = P2_INFO_ENTRY(proc2_idx);
            proc1_pid = entry->level1_pid;
        }

        /* Build info */
        PROC2_$BUILD_INFO_INTERNAL(proc2_idx, proc1_pid, local_info, &status);

        ML_$UNLOCK(PROC2_LOCK_ID);

        /* Cap length at 0xE4 */
        if (len > 0xE4) {
            len = 0xE4;
        }

        /* Copy to output buffer */
        if (len > 0) {
            memcpy(info, local_info, len);
        }
    } else {
        /* Process not found */
        ML_$UNLOCK(PROC2_LOCK_ID);
    }

    *status_ret = status;
}
