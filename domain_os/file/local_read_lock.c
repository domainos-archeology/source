/*
 * FILE_$LOCAL_READ_LOCK - Read local lock entry data
 *
 * Original address: 0x00E6050E
 * Size: 274 bytes
 *
 * This function reads lock information from the local lock table
 * for a specific file UID. It searches the hash chain for a matching
 * entry and copies the lock information to the output buffer.
 *
 * Assembly analysis:
 *   - link.w A6,-0xc        ; Stack frame
 *   - Calls UID_$HASH to compute hash bucket
 *   - Acquires ML_$LOCK(5) for lock table protection
 *   - Searches hash chain for matching UID
 *   - Copies 34 bytes of lock info to output
 *   - Releases ML_$UNLOCK(5) before returning
 */

#include "file/file_internal.h"
#include "ml/ml.h"

/*
 * Lock entries are reached through FILE_$LOT_ENTRY() (file/file_internal.h).
 * 0x00E6056C `movea.l #0xe935cc,A0` loads the table base - entry 1 - and every
 * field is then read at a NEGATIVE displacement off `base + index*0x1C`, the
 * END of entry `index`.  The private biased base this file used to define has
 * been dropped in favour of the shared 1-based accessor.
 */

/*
 * FILE_$LOCAL_READ_LOCK - Read local lock entry data
 *
 * Searches the local lock table for a lock on the specified file
 * and returns the lock information.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   info_out   - Output buffer for lock info (34 bytes)
 *   status_ret - Output: status code
 *                status_$ok if found,
 *                file_$object_not_locked_by_this_process if not found
 */
void FILE_$LOCAL_READ_LOCK(uid_t *file_uid, file_lock_info_internal_t *info_out,
                            status_$t *status_ret)
{
    int16_t hash_index;
    int16_t entry_idx;
    file_lock_entry_detail_t *entry;

    /* Compute hash bucket for the file UID */
    hash_index = UID_$HASH(file_uid, NULL);

    /* Default status: not found */
    *status_ret = file_$object_not_locked_by_this_process;

    /* Acquire lock table spinlock */
    ML_$LOCK(5);

    /* Get head of hash chain */
    entry_idx = FILE_$LOT_HASHTAB[hash_index];

    /* Iterate through hash chain */
    while (entry_idx > 0) {
        /* Entry `entry_idx` of the 1-based table (0x00E6056C). */
        entry = FILE_$LOT_ENTRY(entry_idx);

        /* Compare UIDs */
        if (entry->uid_high == file_uid->high && entry->uid_low == file_uid->low) {
            /*
             * Found matching entry - copy lock information
             */

            /* File UID */
            info_out->file_uid.high = entry->uid_high;
            info_out->file_uid.low = entry->uid_low;

            /* Context: entry +0x00, read at (-0x1c,An) off the entry end */
            info_out->context = entry->context;

            /* Lock side: bit 7 of flags2 (entry +0x1B, (-0x1,An)) */
            uint8_t flags2 = entry->flags2;
            info_out->side = (flags2 >> 7) & 1;

            /* Lock mode: bits 3-6 of flags2 */
            info_out->mode = (flags2 & 0x78) >> 3;

            /* Sequence number: entry +0x16, read at (-0x6,An) */
            info_out->sequence = entry->sequence;

            /*
             * Node/port information depends on remote flag (bit 2 of flags2)
             */
            uint8_t remote_flag = flags2 & 0x04;

            if (remote_flag) {
                /*
                 * Remote lock: entry has remote holder info, local is owner
                 * holder_node/port = entry.node_low/high (remote holder)
                 * owner_node = NODE_$ME (we are the owner)
                 * remote_info = ROUTE_$PORT
                 */
                info_out->holder_node = entry->node_low;
                info_out->holder_port = entry->node_high;
                info_out->owner_node = NODE_$ME;
                info_out->remote_info = ROUTE_$PORT;
            } else {
                /*
                 * Local lock: local node is holder
                 * holder_node/port = NODE_$ME/ROUTE_$PORT (we are the holder)
                 * owner_node = entry.node_low (who locked it)
                 * remote_info = entry.node_high
                 */
                info_out->holder_node = NODE_$ME;
                info_out->holder_port = ROUTE_$PORT;
                info_out->owner_node = entry->node_low;
                info_out->remote_info = entry->node_high;
            }

            /* Success */
            *status_ret = status_$ok;
            goto done;
        }

        /* Move to next entry in chain (entry +0x14, read at (-0x8,An)) */
        entry_idx = (int16_t)entry->next;
    }

done:
    /* Release lock table spinlock */
    ML_$UNLOCK(5);
}
