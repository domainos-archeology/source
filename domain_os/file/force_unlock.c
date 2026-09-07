/*
 * FILE_$FORCE_UNLOCK - Force unlock a file
 *
 * Original address: 0x00E60DB0
 * Size: 142 bytes
 *
 * Forces release of a lock, even if not held by the current process.
 * This is typically used for administrative cleanup of stale locks.
 * Only works for locks where the node ID matches local node.
 *
 * Parameters:
 *   file_uid   - UID of file to unlock
 *   status_ret - Output status code
 *
 * Assembly analysis:
 *   - Calls FILE_$READ_LOCK_ENTRYUI to get lock info
 *   - Validates that lock is on local node (NODE_$ME)
 *   - Validates that lock was created from different node
 *   - Calls FILE_$PRIV_UNLOCK with remote_flags=-1
 *   - Maps file_$object_not_locked_by_this_process to 0
 */

#include "file/file_internal.h"

/*
 * FILE_$FORCE_UNLOCK - Force unlock a file
 */
void FILE_$FORCE_UNLOCK(uid_t *file_uid, status_$t *status_ret)
{
    uid_t local_uid;
    uint32_t dtv_out[2];

    /*
     * FILE_$READ_LOCK_ENTRYUI's output record (A6-0x28), the same
     * file_lock_info_internal_t every other reader of that routine uses:
     * 0x00E60DEA reads the holder node at +0x16, 0x00E60DF6 the owner node at
     * +0x0C, 0x00E60E10 the context at +0x08 and 0x00E60E14 the sequence at
     * +0x14.
     */
    file_lock_info_internal_t lock_info;

    /* Copy file UID to local */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /*
     * Get lock info for this file (unchecked access)
     */
    FILE_$READ_LOCK_ENTRYUI(&local_uid, &lock_info, status_ret);

    if (*status_ret == status_$ok) {
        /*
         * 0x00E60DE4-0x00E60E04: the lock must be held on this node
         * (holder_node == NODE_$ME) and have been taken from another node
         * (owner_node & 0xFFFFF != NODE_$ME); anything else is refused.
         */
        if ((NODE_$ME == lock_info.holder_node) &&
            ((lock_info.owner_node & 0xFFFFF) != NODE_$ME)) {
            /*
             * 0x00E60E06-0x00E60E22, pushed right to left:
             *   pea (A2)            status_ret
             *   pea (-0x38,A6)      dtv_out
             *   move.l (-0x1c,A6)   rem_node  = lock_info.owner_node
             *   move.l (-0x20,A6)   rem_key   = lock_info.context
             *   move.w (-0x14,A6)   key       = lock_info.sequence
             *   st                  by_key    = TRUE
             *   clr.l               lock_mode = 0, asid = 0
             *   clr.l               lock_slot = 0
             *   pea (-0x30,A6)      file_uid  = &local_uid
             */
            (void)FILE_$PRIV_UNLOCK(&local_uid,
                                    0,                      /* lock_slot */
                                    0,                      /* lock_mode: any */
                                    0,                      /* asid      */
                                    -1,                     /* by_key    */
                                    lock_info.sequence,     /* key       */
                                    lock_info.context,      /* rem_key   */
                                    lock_info.owner_node,   /* rem_node  */
                                    dtv_out,
                                    status_ret);
        } else {
            /*
             * Cannot force unlock - lock is either:
             * - Not managed by this node, or
             * - Was created locally (not a remote lock)
             */
            *status_ret = file_$op_cannot_perform_here;
        }
    }

    /*
     * Map not-locked status to success
     * file_$object_not_locked_by_this_process = 0x0F0005
     */
    if (*status_ret == file_$object_not_locked_by_this_process) {
        *status_ret = 0;
    }
}
