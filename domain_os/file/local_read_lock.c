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
 * Lock table base addresses
 */
#define LOT_DATA_BASE       0xE935B0
#define LOT_ENTRY_SIZE      0x1C

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
    uint8_t *entry_base;
    int32_t entry_offset;

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
        /*
         * Compute entry base address
         * Entry N is at LOT_DATA_BASE + N * 0x1C
         * We access data relative to the end of the entry (+ 0x1C offset)
         */
        entry_offset = (int32_t)entry_idx * LOT_ENTRY_SIZE;
        entry_base = (uint8_t *)(LOT_DATA_BASE + LOT_ENTRY_SIZE + entry_offset);

        /* Get pointers to entry fields */
        uint32_t *uid_high_ptr = (uint32_t *)(entry_base - 0x10);
        uint32_t *uid_low_ptr = (uint32_t *)(entry_base - 0x0C);
        int16_t *next_ptr = (int16_t *)(entry_base - 0x08);

        /* Compare UIDs */
        if (*uid_high_ptr == file_uid->high && *uid_low_ptr == file_uid->low) {
            /*
             * Found matching entry - copy lock information
             */

            /* File UID */
            info_out->file_uid.high = *uid_high_ptr;
            info_out->file_uid.low = *uid_low_ptr;

            /* Context: at offset -0x1C from entry end */
            info_out->context = *(uint32_t *)(entry_base - 0x1C);

            /* Lock side: bit 7 of flags2 (at -0x01) */
            uint8_t flags2 = *(entry_base - 0x01);
            info_out->side = (flags2 >> 7) & 1;

            /* Lock mode: bits 3-6 of flags2 */
            info_out->mode = (flags2 & 0x78) >> 3;

            /* Sequence number: at offset -0x06 */
            info_out->sequence = *(uint16_t *)(entry_base - 0x06);

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
                info_out->holder_node = *(uint32_t *)(entry_base - 0x18);
                info_out->holder_port = *(uint32_t *)(entry_base - 0x14);
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
                info_out->owner_node = *(uint32_t *)(entry_base - 0x18);
                info_out->remote_info = *(uint32_t *)(entry_base - 0x14);
            }

            /* Success */
            *status_ret = status_$ok;
            goto done;
        }

        /* Move to next entry in chain */
        entry_idx = *next_ptr;
    }

done:
    /* Release lock table spinlock */
    ML_$UNLOCK(5);
}
