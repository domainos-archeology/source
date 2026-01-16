/*
 * FILE_$LOCK_D - Lock a file with domain context
 *
 * Original address: 0x00E5EA2A
 * Size: 116 bytes
 *
 * This is a wrapper function that calls FILE_$PRIV_LOCK with specific
 * flags for domain (distributed) locking operations.
 *
 * Assembly analysis:
 *   - link.w A6,-0x4          ; 4 bytes local stack
 *   - Calls FILE_$PRIV_LOCK at 0x00E5F0EE with flags=0x40000 (upgrade)
 *   - If AUDIT_$ENABLED < 0, calls FILE_$AUDIT_LOCK at 0x00E5E88A
 */

#include "file/file_internal.h"

/*
 * FILE_$LOCK_D - Lock a file with domain context
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to lock mode
 *   rights       - Pointer to rights byte
 *   param_5      - Additional parameter (context)
 *   status_ret   - Output status code
 */
void FILE_$LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                  uint8_t *rights, uint32_t param_5, status_$t *status_ret)
{
    uint16_t result;

    /*
     * Call FILE_$PRIV_LOCK with:
     *   - PROC1_$AS_ID as the process ASID
     *   - *lock_index as the lock table index
     *   - *lock_mode as the lock mode
     *   - *rights as the rights byte (extended to 16-bit with upper byte from stack)
     *   - 0x40000 as flags (FILE_LOCK_FLAG_UPGRADE)
     *   - param_5 as the output context
     *   - Other parameters zeroed for local lock
     */
    FILE_$PRIV_LOCK(file_uid,
                    PROC1_$AS_ID,
                    *lock_index,
                    *lock_mode,
                    (int16_t)*rights,     /* Rights byte, sign-extended */
                    0x40000,              /* flags = upgrade mode */
                    0,                    /* param_7 = 0 (local) */
                    0,                    /* param_8 = 0 (no node) */
                    0,                    /* param_9 = 0 */
                    NULL,                 /* param_10 = &DAT_00e5e61e (default addr) */
                    0,                    /* param_11 = 0 */
                    &param_5,             /* lock_ptr_out */
                    &result,              /* result_out */
                    status_ret);

    /*
     * If auditing is enabled (AUDIT_$ENABLED has high bit set),
     * log the lock operation
     */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_LOCK(*status_ret, file_uid, *lock_mode);
    }
}
