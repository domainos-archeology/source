/*
 * FILE_$CHANGE_LOCK_D - Change an existing lock with domain context
 *
 * Original address: 0x00E5EA9E
 * Size: 130 bytes
 *
 * This is a wrapper function that calls FILE_$PRIV_LOCK with flags
 * for changing an existing lock mode. It first validates that the
 * requested lock mode is not in the illegal mode mask.
 *
 * Assembly analysis:
 *   - link.w A6,-0x4          ; 4 bytes local stack
 *   - Checks DAT_00e823f0 (FILE_$LOCK_ILLEGAL_MASK) against lock_mode
 *   - If illegal, returns status_$file_illegal_lock_request (0xF0007)
 *   - Otherwise calls FILE_$PRIV_LOCK with flags=0x440000 (change+upgrade)
 *   - If AUDIT_$ENABLED < 0, calls FILE_$AUDIT_LOCK
 */

#include "file/file_internal.h"

/*
 * FILE_$CHANGE_LOCK_D - Change an existing lock's mode
 *
 * Parameters:
 *   file_uid     - UID of file with existing lock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to new lock mode
 *   param_4      - Additional parameter (context)
 *   status_ret   - Output status code
 */
void FILE_$CHANGE_LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                         uint32_t param_4, status_$t *status_ret)
{
    uint16_t result;
    uint16_t mode = *lock_mode;

    /*
     * Check if the requested lock mode is illegal.
     * FILE_$LOCK_ILLEGAL_MASK is a bitmask where bit N is set if mode N is illegal.
     * The original code: btst.l D0,D1 where D0 = mode, D1 = illegal_mask
     */
    if ((FILE_$LOCK_ILLEGAL_MASK & (1 << (mode & 0x1F))) != 0) {
        *status_ret = status_$file_illegal_lock_request;
    } else {
        /*
         * Call FILE_$PRIV_LOCK with:
         *   - PROC1_$AS_ID as the process ASID
         *   - *lock_index as the lock table index
         *   - mode as the lock mode
         *   - 0 as the rights byte (not used for change)
         *   - 0x440000 as flags (FILE_LOCK_FLAG_CHANGE | FILE_LOCK_FLAG_UPGRADE)
         *   - param_4 as the output context
         *   - Other parameters zeroed for local lock
         */
        FILE_$PRIV_LOCK(file_uid,
                        PROC1_$AS_ID,
                        *lock_index,
                        mode,
                        0,                /* rights = 0 (not used for change) */
                        0x440000,         /* flags = change + upgrade mode */
                        0,                /* param_7 = 0 (local) */
                        0,                /* param_8 = 0 (no node) */
                        0,                /* param_9 = 0 */
                        NULL,             /* param_10 = &DAT_00e5e61e (default addr) */
                        0,                /* param_11 = 0 */
                        &param_4,         /* lock_ptr_out */
                        &result,          /* result_out */
                        status_ret);
    }

    /*
     * If auditing is enabled (AUDIT_$ENABLED has high bit set),
     * log the lock change operation
     */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_LOCK(*status_ret, file_uid, mode);
    }
}
