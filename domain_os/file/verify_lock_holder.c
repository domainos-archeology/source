/*
 * FILE_$VERIFY_LOCK_HOLDER - Verify lock holder is still valid
 *
 * Original address: 0x00E60732
 * Size: 234 bytes
 *
 * This function verifies that a lock entry's holder is still valid.
 * If the holder has released the lock (detected via remote or local
 * verification), this function will clean up the stale lock entry.
 *
 * This is used by FILE_$READ_LOCK_ENTRYI and FILE_$READ_LOCK_ENTRYUI
 * to ensure that returned lock information is still accurate.
 *
 * Assembly analysis:
 *   - link.w A6,-0x3c       ; Stack frame
 *   - Compares owner_node with holder_node
 *   - If different, calls verification (local or remote)
 *   - If lock was released, calls unlock to clean up
 */

#include "file/file_internal.h"

/* file_lock_info_internal_t is defined in file_internal.h */

/*
 * FILE_$VERIFY_LOCK_HOLDER - Verify lock holder is still valid
 *
 * Checks if the lock holder is still holding the lock. If the lock
 * has been released, cleans up the stale entry.
 *
 * Parameters:
 *   lock_info  - Lock information structure (from read operations)
 *   status_ret - Output: status_$ok if still valid,
 *                        status_$file_object_not_locked_by_this_process if released
 *
 * Algorithm:
 *   1. Compare owner_node with holder_node
 *   2. If same, lock is valid (self-owned)
 *   3. If different, verify with holder (local or remote)
 *   4. If holder says "not locked", call unlock to clean up
 *   5. If unlock succeeds, return "not locked" to signal retry
 */
void FILE_$VERIFY_LOCK_HOLDER(file_lock_info_internal_t *lock_info, status_$t *status_ret)
{
    status_$t verify_status;
    uint32_t owner_node_low;

    /*
     * Extract owner node (low 20 bits of owner_node field)
     * This is at offset 0x0C in the structure (param_1[3] & 0xFFFFF)
     */
    owner_node_low = lock_info->owner_node & 0xFFFFF;

    /*
     * Compare with holder node at offset 0x16
     * If they're the same, the lock is self-owned and valid
     */
    if (owner_node_low == lock_info->holder_node) {
        *status_ret = status_$ok;
        return;
    }

    /*
     * Different nodes - need to verify with the holder
     */
    if (owner_node_low == NODE_$ME) {
        /* Local holder - verify locally */
        FILE_$LOCAL_LOCK_VERIFY((void *)lock_info, &verify_status);
    } else {
        /* Remote holder - verify via RPC */
        struct {
            uint32_t port;
            uint32_t node;
        } node_info;

        node_info.port = lock_info->remote_port;
        node_info.node = owner_node_low;

        REM_FILE_$LOCAL_VERIFY(&node_info, lock_info, &verify_status);
    }

    /*
     * If verification returns "not locked by this process",
     * the lock was released - need to clean up
     */
    if (verify_status == status_$file_object_not_locked_by_this_process) {
        status_$t unlock_status;
        uint32_t holder_node;

        /*
         * Get holder node info for the unlock call
         */
        holder_node = lock_info->holder_node;

        if (holder_node == NODE_$ME) {
            /*
             * Local holder - unlock locally
             * Call FILE_$PRIV_UNLOCK with the lock parameters
             */
            uint32_t dtv_out[2];
            uint32_t mode_asid;

            /*
             * Build mode_asid: high word = lock_mode, low word = sequence
             * But use 0xFF for certain flags (from assembly analysis)
             */
            mode_asid = ((uint32_t)lock_info->mode << 16);

            FILE_$PRIV_UNLOCK(&lock_info->file_uid,
                              0,                            /* lock_index = 0 (search) */
                              mode_asid,
                              -1,                           /* remote_flags (negative) */
                              lock_info->context,           /* param_5 = context */
                              lock_info->owner_node,        /* param_6 = owner_node */
                              dtv_out,
                              &unlock_status);
        } else {
            /*
             * Remote holder - unlock via RPC
             */
            struct {
                uint32_t node;
                uint32_t port;
                uid_t    file_uid;
            } unlock_params;

            unlock_params.node = holder_node;
            unlock_params.port = lock_info->holder_port;
            unlock_params.file_uid.high = lock_info->file_uid.high;
            unlock_params.file_uid.low = lock_info->file_uid.low;

            uint8_t result_buf[8];

            REM_FILE_$UNLOCK(result_buf,
                             lock_info->mode,
                             lock_info->context,
                             lock_info->sequence,
                             lock_info->owner_node,
                             0,                            /* modified_flag = 0 */
                             &unlock_status);
        }

        /*
         * If unlock succeeded, return the original "not locked" status
         * to signal that the caller should retry
         */
        if (unlock_status == status_$ok) {
            *status_ret = status_$file_object_not_locked_by_this_process;
            return;
        }
    }

    /* Lock is still valid (or verification succeeded) */
    *status_ret = status_$ok;
}
