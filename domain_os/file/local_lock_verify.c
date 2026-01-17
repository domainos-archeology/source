/*
 * FILE_$LOCAL_LOCK_VERIFY - Verify local lock ownership
 *
 * Original address: 0x00E6081C
 * Size: 208 bytes
 *
 * This function verifies that a file is locked by the current process.
 * It searches the local lock table for a matching lock entry.
 *
 * The function checks:
 *   1. The file UID matches
 *   2. The lock side (reader/writer) matches the request
 *   3. Either the process ASID matches, or the process is in the
 *      same group (via the ASID map table)
 *
 * Assembly analysis:
 *   - link.w A6,-0xc        ; Stack frame
 *   - Calls UID_$HASH to compute hash bucket
 *   - Acquires ML_$LOCK(5) for lock table protection
 *   - Iterates through hash chain checking for match
 *   - Releases ML_$UNLOCK(5) before returning
 */

#include "file/file_internal.h"
#include "ml/ml.h"

/*
 * Lock entry data base addresses
 * Entries are at DAT_00e935b0 with 0x1C byte stride
 */
#define LOT_DATA_BASE       0xE935B0
#define LOT_ENTRY_SIZE      0x1C

/*
 * Offsets within lock entry (relative to base + index * 0x1C + 0x1C)
 *   -0x10 (0x0C): uid_high
 *   -0x0C (0x10): uid_low
 *   -0x08 (0x14): next pointer
 *   -0x06 (0x16): sequence
 *   -0x04 (0x18): refcount
 *   -0x03 (0x19): flags1 (bits 0-5 = rights mask, bit 6 unused, bit 7 = remote)
 *   -0x01 (0x1B): flags2 (bit 7 = side, bits 3-6 = mode, bit 2 = remote, etc)
 */

/*
 * ASID group mapping table (at FILE_$LOCK_CONTROL + 0x40)
 * Maps ASID to group, used for checking if two processes share locks
 */
extern uint16_t FILE_$ASID_MAP[];

/*
 * FILE_$LOCAL_LOCK_VERIFY - Verify local lock ownership
 *
 * Checks if the specified file is locked by the process identified
 * in the request structure.
 *
 * Parameters:
 *   request    - Lock verification request containing file UID and process info
 *   status_ret - Output: status_$ok if locked by process,
 *                        status_$file_object_not_locked_by_this_process otherwise
 */
void FILE_$LOCAL_LOCK_VERIFY(lock_verify_request_t *request, status_$t *status_ret)
{
    int16_t hash_index;
    int16_t entry_idx;
    uint8_t *entry_base;
    int32_t entry_offset;
    int8_t found = 0;

    /* Compute hash bucket for the file UID */
    hash_index = UID_$HASH(&request->file_uid, NULL);

    /* Default status: not locked by this process */
    *status_ret = status_$file_object_not_locked_by_this_process;

    /* Acquire lock table spinlock */
    ML_$LOCK(5);

    /* Get head of hash chain */
    entry_idx = FILE_$LOT_HASHTAB[hash_index];

    /*
     * Check if lock table is in "full bypass" mode (DAT_00e823f2)
     * If set, any lock is considered valid
     */
    if (FILE_$LOT_FULL != 0) {
        /* Bypass mode - report as locked */
        *status_ret = status_$ok;
        goto done;
    }

    /* Iterate through hash chain */
    while (entry_idx > 0) {
        /*
         * Compute entry base address
         * Entry N is at LOT_DATA_BASE + N * 0x1C
         * We access data relative to the end of the entry (+ 0x1C offset)
         */
        entry_offset = (int32_t)entry_idx * LOT_ENTRY_SIZE;
        entry_base = (uint8_t *)(LOT_DATA_BASE + LOT_ENTRY_SIZE + entry_offset);

        /*
         * Check if this entry matches the file UID
         */
        uint32_t *uid_high_ptr = (uint32_t *)(entry_base - 0x10);
        uint32_t *uid_low_ptr = (uint32_t *)(entry_base - 0x0C);
        int16_t *next_ptr = (int16_t *)(entry_base - 0x08);
        uint8_t *flags2_ptr = entry_base - 0x01;

        /* Compare UIDs */
        found = 0;
        if (*uid_high_ptr == request->file_uid.high) {
            if (*uid_low_ptr == request->file_uid.low) {
                found = -1;  /* UID matches */
            }
        }

        /*
         * Check if lock side matches (bit 7 of flags2)
         */
        uint16_t entry_side = (*flags2_ptr >> 7) & 1;
        int8_t side_match = (entry_side == request->side) ? -1 : 0;

        /*
         * Both UID and side must match
         */
        if ((found & side_match) < 0) {
            /*
             * Check process ownership:
             * Either ASID matches directly, or
             * (remote flag clear AND ASID maps to same group)
             */
            uint8_t *flags1_ptr = entry_base - 0x03;
            uint16_t entry_mode = ((*flags2_ptr) & 0x78) >> 3;  /* Bits 3-6 = mode */
            uint8_t remote_flag = (*flags2_ptr) & 0x02;  /* Bit 1 = pending/remote */

            if (entry_mode == request->asid) {
                /* Direct ASID match */
                *status_ret = status_$ok;
                goto done;
            }

            if (remote_flag == 0) {
                /*
                 * Not a remote/pending lock - check ASID group mapping
                 * The map table translates ASIDs to groups
                 */
                uint16_t entry_group = FILE_$ASID_MAP[entry_mode];
                if (request->asid == entry_group) {
                    *status_ret = status_$ok;
                    goto done;
                }
            }
        }

        /* Move to next entry in chain */
        entry_idx = *next_ptr;
    }

done:
    /* Release lock table spinlock */
    ML_$UNLOCK(5);
}
