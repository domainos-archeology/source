/*
 * FILE_$UNLOCK_D - Unlock a file with domain context
 *
 * Original address: 0x00E5FCC2
 * Size: 58 bytes
 *
 * This is a wrapper function that calls FILE_$PRIV_UNLOCK with
 * the lock index and mode from the domain lock.
 *
 * Assembly analysis:
 *   - link.w A6,-0x8          ; 8 bytes local stack
 *   - Calls FILE_$PRIV_UNLOCK at 0x00E5FD32
 *   - Combines lock_mode and PROC1_$AS_ID into mode_asid parameter
 */

#include "file/file_internal.h"

/*
 * FILE_$UNLOCK_D - Unlock a file with domain context
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_index   - Pointer to lock index (32-bit, contains index in low word)
 *   lock_mode    - Pointer to lock mode
 *   status_ret   - Output status code
 */
void FILE_$UNLOCK_D(uid_t *file_uid, uint32_t *lock_index, uint16_t *lock_mode,
                    status_$t *status_ret)
{
    uint32_t dtv_out[2];  /* 8 bytes for data-time-valid output */

    /*
     * Call FILE_$PRIV_UNLOCK with:
     *   - file_uid as the file UID
     *   - (short)*lock_index as the lock table index (low 16 bits)
     *   - Combined mode_asid: (*lock_mode << 16) | PROC1_$AS_ID
     *   - 0 for remote_flags (local operation)
     *   - 0 for param_5 and param_6 (no remote context)
     *   - dtv_out for data-time-valid output
     */
    FILE_$PRIV_UNLOCK(file_uid,
                      (uint16_t)(*lock_index),        /* Lock index */
                      ((uint32_t)*lock_mode << 16) | (uint32_t)PROC1_$AS_ID,  /* mode_asid */
                      0,                              /* remote_flags = 0 */
                      0,                              /* param_5 = 0 */
                      0,                              /* param_6 = 0 */
                      dtv_out,                        /* dtv_out */
                      status_ret);
}
