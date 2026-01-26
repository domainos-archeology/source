/*
 * FILE_$LOCK - Lock a file
 *
 * Original address: 0x00E5EB20
 * Size: 120 bytes
 *
 * This is the standard file locking function. It calls FILE_$PRIV_LOCK
 * with flags for a standard local lock operation.
 *
 * Assembly analysis:
 *   - link.w A6,-0x8          ; 8 bytes local stack
 *   - local_8 cleared to 0 (used as lock context output)
 *   - Calls FILE_$PRIV_LOCK at 0x00E5F0EE with flags=0x240000
 *   - If AUDIT_$ENABLED < 0, calls FILE_$AUDIT_LOCK at 0x00E5E88A
 */

#include "file/file_internal.h"

/*
 * FILE_$LOCK - Lock a file
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to lock mode
 *   rights       - Pointer to rights byte
 *   lock_info    - Output buffer for lock info (8 bytes) - UNUSED by this function
 *                  Note: Callers pass a buffer here, but FILE_$LOCK uses internal
 *                  local variables instead. This parameter is accepted for API
 *                  compatibility but its contents are not populated.
 *   status_ret   - Output status code
 */
void FILE_$LOCK(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                uint8_t *rights, void *lock_info, status_$t *status_ret)
{
    uint32_t local_8 = 0;   /* Lock context output - cleared at start */
    uint16_t result;

    (void)lock_info;  /* Unused parameter - see function comment */

    /*
     * Call FILE_$PRIV_LOCK with:
     *   - PROC1_$AS_ID as the process ASID
     *   - *lock_index as the lock table index
     *   - *lock_mode as the lock mode
     *   - *rights as the rights byte (extended to 16-bit)
     *   - 0x240000 as flags (FILE_LOCK_FLAG_LOCAL_ONLY | FILE_LOCK_FLAG_UPGRADE)
     *   - &local_8 as the output context (different from lock_d which uses param_5)
     *   - Other parameters zeroed for local lock
     */
    FILE_$PRIV_LOCK(file_uid,
                    PROC1_$AS_ID,
                    *lock_index,
                    *lock_mode,
                    (int16_t)*rights,     /* Rights byte, sign-extended */
                    0x240000,             /* flags = local_only + upgrade mode */
                    0,                    /* param_7 = 0 (local) */
                    0,                    /* param_8 = 0 (no node) */
                    0,                    /* param_9 = 0 */
                    NULL,                 /* param_10 = &DAT_00e5e61e (default addr) */
                    0,                    /* param_11 = 0 */
                    &local_8,             /* lock_ptr_out - uses local variable */
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
