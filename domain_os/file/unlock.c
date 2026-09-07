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
     * 0x00E5FD08-0x00E5FD28, pushed right to left:
     *   move.l (0x10,A6)        status_ret
     *   pea (-0x8,A6)           dtv_out
     *   clr.l / clr.l           rem_node, rem_key
     *   clr.l                   by_key, key
     *   move.w PROC1_$AS_ID     asid
     *   move.w (A0)             lock_mode = *lock_mode
     *   clr.l                   lock_slot = 0 (search the process' lock row)
     *   move.l (0x8,A6)         file_uid
     */
    (void)FILE_$PRIV_UNLOCK(file_uid,
                            0,                  /* lock_slot: search    */
                            *lock_mode,         /* lock_mode            */
                            PROC1_$AS_ID,       /* asid                 */
                            0,                  /* by_key = false       */
                            0,                  /* key                  */
                            0,                  /* rem_key              */
                            0,                  /* rem_node             */
                            dtv_out,
                            status_ret);
}
