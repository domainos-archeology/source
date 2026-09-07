/*
 * FILE_$UNLOCK_VOL - Unlock all locks on a volume
 *
 * Original address: 0x00E60D36
 * Size: 122 bytes
 *
 * Releases all locks held on files within a specific volume.
 * Iterates through all locks using FILE_$READ_LOCK_ENTRYI and
 * unlocks each one using FILE_$PRIV_UNLOCK.
 *
 * Parameters:
 *   vol_uid    - UID of volume to unlock
 *   status_ret - Output status code
 *
 * Assembly analysis:
 *   - Uses iteration index starting at 1
 *   - Calls FILE_$READ_LOCK_ENTRYI to get next lock
 *   - Calls FILE_$PRIV_UNLOCK for each lock found
 *   - Loops until status != status_$ok
 *   - Maps file_$obj_not_locked_by_this_process to status_$ok
 */

#include "file/file_internal.h"

/*
 * FILE_$UNLOCK_VOL - Unlock all locks on a volume
 */
void FILE_$UNLOCK_VOL(uid_t *vol_uid, status_$t *status_ret)
{
    uint16_t iter_index;
    uid_t local_uid;
    status_$t local_status;
    uint32_t dtv_out[2];

    /* Lock entry info buffer from FILE_$READ_LOCK_ENTRYI */
    file_lock_info_internal_t lock_info;

    /* Copy volume UID to local for modification */
    local_uid.high = vol_uid->high;
    local_uid.low = vol_uid->low;

    /* Start iteration at index 1 */
    iter_index = 1;

    /*
     * Loop through all locks on this volume
     */
    while (1) {
        /* Get next lock entry */
        FILE_$READ_LOCK_ENTRYI(&local_uid, &iter_index, &lock_info, status_ret);

        if (*status_ret != status_$ok) {
            break;
        }

        /*
         * Unlock this entry.  0x00E60D74-0x00E60D92, pushed right to left
         * (the lock_info record is based at A6-0x28):
         *   pea (-0x3c,A6)            status_ret = &local_status
         *   pea (-0x38,A6)            dtv_out
         *   move.l (-0x1c,A6)         rem_node  = lock_info.owner_node (+0x0C)
         *   move.l (-0x20,A6)         rem_key   = lock_info.context    (+0x08)
         *   move.w (-0x14,A6)         key       = lock_info.sequence   (+0x14)
         *   st                        by_key    = TRUE
         *   clr.l                     lock_mode = 0, asid = 0
         *   clr.l                     lock_slot = 0
         *   pea (-0x28,A6)            file_uid  = &lock_info.file_uid
         */
        (void)FILE_$PRIV_UNLOCK((uid_t *)(void *)&lock_info,
                                0,                      /* lock_slot        */
                                0,                      /* lock_mode: any   */
                                0,                      /* asid             */
                                -1,                     /* by_key = true    */
                                lock_info.sequence,     /* key              */
                                lock_info.context,      /* rem_key          */
                                lock_info.owner_node,   /* rem_node         */
                                dtv_out,
                                &local_status);
    }

    /*
     * Map not-locked status to success
     * file_$obj_not_locked_by_this_process = 0x0F000C
     */
    if (*status_ret == file_$obj_not_locked_by_this_process) {
        *status_ret = status_$ok;
    }
}
