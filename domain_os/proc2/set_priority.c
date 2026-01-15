/*
 * PROC2_$SET_PRIORITY - Set process priority range
 *
 * Sets min and max priority for process, ensuring min <= max by
 * swapping if needed. Calls PROC1_$SET_PRIORITY with the process's
 * level1_pid.
 *
 * Parameters:
 *   proc_uid   - Pointer to process UID
 *   priority_1 - Pointer to first priority value
 *   priority_2 - Pointer to second priority value
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e414de
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_PRIORITY(uid_t *proc_uid, uint16_t *priority_1, uint16_t *priority_2,
                         status_$t *status_ret)
{
    int16_t index;
    proc2_info_t *info;
    status_$t status;
    uint16_t min_priority, max_priority;

    /* Ensure min <= max by swapping if needed */
    if (*priority_1 < *priority_2) {
        min_priority = *priority_1;
        max_priority = *priority_2;
    } else {
        min_priority = *priority_2;
        max_priority = *priority_1;
    }

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if ((status >> 16) == 0) {  /* Check high word of status is 0 */
        info = P2_INFO_ENTRY(index);

        /* Call PROC1 with mode -1 (0xff26 from decompilation) to set priority */
        /* The mode value 0xff26 = -218 indicates "set priority" mode */
        PROC1_$SET_PRIORITY(info->level1_pid, (int16_t)0xff26, &min_priority, &max_priority);
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}
