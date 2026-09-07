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
     * 0x00E5FCCE-0x00E5FCF2, pushed right to left:
     *   move.l (0x14,A6)        status_ret
     *   pea (-0x8,A6)           dtv_out
     *   clr.l / clr.l           rem_node, rem_key
     *   clr.l                   by_key, key
     *   move.w PROC1_$AS_ID     asid
     *   move.w (A0)             lock_mode = *lock_mode
     *   move.l (A1)             lock_slot = *lock_index (a full longword)
     *   move.l (0x8,A6)         file_uid
     */
    (void)FILE_$PRIV_UNLOCK(file_uid,
                            (int32_t)*lock_index,   /* lock_slot        */
                            *lock_mode,             /* lock_mode        */
                            PROC1_$AS_ID,           /* asid             */
                            0,                      /* by_key = false   */
                            0,                      /* key              */
                            0,                      /* rem_key          */
                            0,                      /* rem_node         */
                            dtv_out,
                            status_ret);
}
