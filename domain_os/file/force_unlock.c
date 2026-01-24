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

    /* Lock entry info buffer from FILE_$READ_LOCK_ENTRYUI */
    struct {
        uint32_t    context;        /* 0x00 */
        uint32_t    node_low;       /* 0x04 */
        uint32_t    node_high;      /* 0x08 */
        uint32_t    uid_high;       /* 0x0C */
        uint32_t    uid_low;        /* 0x10 */
        uint16_t    next;           /* 0x14 */
        uint16_t    sequence;       /* 0x16 */
        uint8_t     refcount;       /* 0x18 */
        uint8_t     flags1;         /* 0x19 */
        uint8_t     rights;         /* 0x1A */
        uint8_t     flags2;         /* 0x1B */
    } lock_info;

    /* Copy file UID to local */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /*
     * Get lock info for this file (unchecked access)
     */
    FILE_$READ_LOCK_ENTRYUI(&local_uid, &lock_info, status_ret);

    if (*status_ret == status_$ok) {
        /*
         * Validate that this lock can be force-unlocked:
         * 1. The lock's node_high must match NODE_$ME
         * 2. The lock's node_low (masked to 20 bits) must NOT match NODE_$ME
         *
         * This ensures we can only force-unlock locks that are:
         * - Managed by our node (node_high == NODE_$ME)
         * - But were created from a remote node (node_low != NODE_$ME)
         *
         * This prevents accidentally forcing our own local locks.
         */
        if ((NODE_$ME == lock_info.node_high) &&
            ((lock_info.node_low & 0xFFFFF) != NODE_$ME)) {
            /*
             * Valid - call FILE_$PRIV_UNLOCK with remote_flags=-1
             */
            FILE_$PRIV_UNLOCK(&local_uid,
                              0,                     /* lock_index = 0 (search) */
                              (uint32_t)lock_info.sequence << 16,  /* mode_asid */
                              -1,                    /* remote_flags = -1 (remote unlock) */
                              lock_info.context,     /* param_5 = context */
                              lock_info.node_low,    /* param_6 = node address */
                              dtv_out,               /* dtv_out */
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
