/*
 * PROC2_$INFO - Get process info by PID
 *
 * Returns process information for a process identified by its PROC1 PID.
 * If pid is 0, returns info for the system process (index 0).
 *
 * Parameters:
 *   pid        - Pointer to PROC1 PID (0 for system process)
 *   offset     - Pointer to offset value (passed to BUILD_INFO_INTERNAL)
 *   info       - Buffer to receive info (up to 0xE4 bytes)
 *   info_len   - Pointer to buffer length (capped at 0xE4)
 *   status_ret - Pointer to receive status
 *
 * Original address: 0x00e407b4
 */

#include "proc2/proc2_internal.h"

void PROC2_$INFO(int16_t *pid, int16_t *offset, void *info, uint16_t *info_len,
                 status_$t *status_ret)
{
    uint16_t len;
    int16_t proc2_idx;
    int16_t target_pid;
    proc2_info_t *entry;
    uint8_t local_info[0xE4];
    status_$t status;

    len = *info_len;
    target_pid = *pid;

    /* Find process index */
    if (target_pid == 0) {
        /* System process */
        proc2_idx = 0;
    } else {
        /* Search allocation list for process with matching ASID */
        proc2_idx = P2_INFO_ALLOC_PTR;
        while (proc2_idx != 0) {
            entry = P2_INFO_ENTRY(proc2_idx);
            if (entry->asid == target_pid) {
                break;
            }
            proc2_idx = entry->next_index;
        }
    }

    /* Build info under lock */
    ML_$LOCK(PROC2_LOCK_ID);
    PROC2_$BUILD_INFO_INTERNAL(proc2_idx, *offset, local_info, &status);
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Cap length at 0xE4 */
    if (len > 0xE4) {
        len = 0xE4;
    }

    /* Copy to output buffer */
    if (len > 0) {
        __builtin_memcpy(info, local_info, len);
    }

    *status_ret = status;
}
