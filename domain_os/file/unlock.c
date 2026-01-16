/*
 * FILE_$UNLOCK - Unlock a file
 *
 * Original address: 0x00E5FCFC
 * Size: 54 bytes
 *
 * This is the standard file unlock function. It calls FILE_$PRIV_UNLOCK
 * with a lock index of 0 (search for lock).
 *
 * Assembly analysis:
 *   - link.w A6,-0x8          ; 8 bytes local stack
 *   - Calls FILE_$PRIV_UNLOCK at 0x00E5FD32
 *   - Passes 0 for lock_index (search for lock by mode)
 */

#include "file/file_internal.h"

/*
 * FILE_$UNLOCK - Unlock a file
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_mode    - Pointer to lock mode
 *   status_ret   - Output status code
 */
void FILE_$UNLOCK(uid_t *file_uid, uint16_t *lock_mode, status_$t *status_ret)
{
    uint32_t dtv_out[2];  /* 8 bytes for data-time-valid output */

    /*
     * Call FILE_$PRIV_UNLOCK with:
     *   - file_uid as the file UID
     *   - 0 as the lock table index (search for matching lock)
     *   - Combined mode_asid: (*lock_mode << 16) | PROC1_$AS_ID
     *   - 0 for remote_flags (local operation)
     *   - 0 for param_5 and param_6 (no remote context)
     *   - dtv_out for data-time-valid output
     */
    FILE_$PRIV_UNLOCK(file_uid,
                      0,                              /* Lock index = 0 (search) */
                      ((uint32_t)*lock_mode << 16) | (uint32_t)PROC1_$AS_ID,  /* mode_asid */
                      0,                              /* remote_flags = 0 */
                      0,                              /* param_5 = 0 */
                      0,                              /* param_6 = 0 */
                      dtv_out,                        /* dtv_out */
                      status_ret);
}
