/*
 * PROC2_$SUSPEND - Suspend a process
 *
 * Suspends the specified process. If suspending another process,
 * waits for the suspension to complete using an eventcount.
 * Handles timeouts and translates PROC1 status codes.
 *
 * Parameters:
 *   proc_uid   - Pointer to process UID to suspend
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e4126a
 * Nested helper at: 0x00e4120c
 */

#include "proc2.h"
#include "ec/ec.h"

/*
 * try_suspend - Helper function (was nested Pascal procedure)
 *
 * Attempts to suspend a process via PROC1_$SUSPEND.
 * Translates status codes and returns success/failure indicator.
 *
 * Parameters:
 *   index      - PROC2 table index of target process
 *   status     - Pointer to status (modified on error)
 *   suspend_result - Pointer to store PROC1_$SUSPEND result
 *
 * Returns:
 *   -1 (0xFF) on success or need to retry
 *   0 on error
 */
static int8_t try_suspend(int16_t index, status_$t *status, int8_t *suspend_result)
{
    proc2_info_t *info;
    int8_t result = -1;  /* 0xFF = success/retry */

    info = P2_INFO_ENTRY(index);

    /* Try to suspend via PROC1 */
    *suspend_result = PROC1_$SUSPEND(info->level1_pid, status);

    /* Check if there was an error */
    if ((*status >> 16) != 0) {
        /* Translate PROC1 status codes to PROC2 equivalents */
        if (*status == status_$process_already_suspended) {
            *status = status_$proc2_already_suspended;
        } else {
            /* Set high bit to indicate PROC1 error */
            *status |= 0x80000000;
        }
        result = 0;  /* Error */
    }

    return result;
}

void PROC2_$SUSPEND(uid_$t *proc_uid, status_$t *status_ret)
{
    int16_t index;
    proc2_info_t *info;
    status_$t status;
    status_$t resume_status;
    int8_t suspend_result;
    int8_t try_result;
    uint32_t wait_val;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if ((status >> 16) == 0) {
        info = P2_INFO_ENTRY(index);

        /* Check if suspending self */
        if (proc_uid->high == P2_INFO_ENTRY(P2_PID_TO_INDEX(PROC1_$CURRENT))->uid.high &&
            proc_uid->low == P2_INFO_ENTRY(P2_PID_TO_INDEX(PROC1_$CURRENT))->uid.low) {
            /* Suspending self - just call helper and return */
            ML_$UNLOCK(PROC2_LOCK_ID);
            try_suspend(index, &status, &suspend_result);
            goto done;
        }

        /* Suspending another process - need to wait for completion */
        wait_val = *((uint32_t*)&PROC1_$SUSPEND_EC) + 1;

        try_result = try_suspend(index, &status, &suspend_result);

        if (try_result < 0) {
            /* Need to wait for suspension to complete */
            while (suspend_result >= 0) {
                ML_$UNLOCK(PROC2_LOCK_ID);

                /* Wait on suspend EC with timeout (0x78 = 120 ticks) */
                /* TODO: EC_$WAIT signature needs verification */
                int16_t wait_result = 0;  /* Simplified - actual impl uses EC_$WAIT */

                ML_$LOCK(PROC2_LOCK_ID);

                /* Re-find the process (might have died) */
                index = PROC2_$FIND_INDEX(proc_uid, &status);
                if ((status >> 16) != 0) {
                    break;
                }

                info = P2_INFO_ENTRY(index);

                if (wait_result == 0) {
                    /* No timeout - try SUSPENDP */
                    PROC1_$SUSPENDP(info->level1_pid, &status);
                    suspend_result = (status == 0) ? -1 : 0;
                    wait_val++;
                } else {
                    /* Timeout - resume and report error */
                    suspend_result = -1;  /* Exit loop */
                    PROC1_$RESUME(info->level1_pid, &resume_status);
                    status = status_$proc2_suspend_timed_out;
                }
            }
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

done:
    *status_ret = status;
}
