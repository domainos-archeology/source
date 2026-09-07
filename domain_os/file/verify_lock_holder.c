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
 *                        file_$object_not_locked_by_this_process if released
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

        node_info.port = lock_info->remote_info;
        node_info.node = owner_node_low;

        REM_FILE_$LOCAL_VERIFY(&node_info, lock_info, &verify_status);
    }

    /*
     * If verification returns "not locked by this process",
     * the lock was released - need to clean up
     */
    if (verify_status == file_$object_not_locked_by_this_process) {
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

            /*
             * 0x00E6079E-0x00E607BE, pushed right to left (A2 = lock_info):
             *   pea (-0x3c,A6)      status_ret = &unlock_status
             *   pea (-0x38,A6)      dtv_out
             *   move.l (0xc,A2)     rem_node  = lock_info->owner_node
             *   move.l (0x8,A2)     rem_key   = lock_info->context
             *   move.w (0x14,A2)    key       = lock_info->sequence
             *   st                  by_key    = TRUE
             *   clr.w               asid      = 0
             *   move.w (0x12,A2)    lock_mode = lock_info->mode
             *   clr.l               lock_slot = 0
             *   pea (A2)            file_uid  = &lock_info->file_uid
             */
            (void)FILE_$PRIV_UNLOCK((uid_t *)(void *)lock_info,
                                    0,                      /* lock_slot */
                                    lock_info->mode,        /* lock_mode */
                                    0,                      /* asid      */
                                    -1,                     /* by_key    */
                                    lock_info->sequence,    /* key       */
                                    lock_info->context,     /* rem_key   */
                                    lock_info->owner_node,  /* rem_node  */
                                    dtv_out,
                                    &verify_status);
        } else {
            /*
             * Remote holder - unlock through REM_FILE_$UNLOCK.
             *
             * 0x00E607C8-0x00E607D8 builds a bare file_$obj_loc_t at A6-0x30:
             *   move.l D0,(-0x1c,A6)          desc.node     = holder_node
             *   move.l (0x1a,A2),(-0x20,A6)   desc.loc_info = holder_port
             *   lea (A2),A0 / two move.l (A0)+ desc.uid     = lock_info->file_uid
             * Nothing else in the 32-byte record is initialised - the original
             * hands out uninitialised stack for +0x00..+0x07 and +0x18..+0x1F,
             * which REM_FILE_$UNLOCK never reads.
             *
             * 0x00E607DC-0x00E607F8, pushed right to left:
             *   subq.l #0x2,SP      result slot (the returned byte is dropped)
             *   pea (-0x3c,A6)      status      = &verify_status
             *   clr.w               release     = FALSE
             *   move.l (0xc,A2)     rem_node    = lock_info->owner_node
             *   move.w (0x14,A2)    lock_key    = lock_info->sequence
             *   move.l (0x8,A2)     rem_key     = lock_info->context
             *   move.w (0x12,A2)    unlock_mode = lock_info->mode
             *   pea (-0x30,A6)      location_block = &desc
             */
            file_$obj_loc_t desc;

            desc.node     = holder_node;
            desc.loc_info = lock_info->holder_port;
            desc.uid      = lock_info->file_uid;

            (void)REM_FILE_$UNLOCK(&desc,
                                   lock_info->mode,        /* unlock_mode */
                                   lock_info->context,     /* rem_key     */
                                   lock_info->sequence,    /* lock_key    */
                                   lock_info->owner_node,  /* rem_node    */
                                   false,                  /* release     */
                                   &verify_status);
        }

        /*
         * If unlock succeeded, return the original "not locked" status
         * to signal that the caller should retry
         */
        if (verify_status == status_$ok) {
            *status_ret = file_$object_not_locked_by_this_process;
            return;
        }
    }

    /* Lock is still valid (or verification succeeded) */
    *status_ret = status_$ok;
}
