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
 * Lock entries are reached through FILE_$LOT_ENTRY() (file/file_internal.h).
 * 0x00E6087A `movea.l #0xe935cc,A0` loads the table base - entry 1 - and the
 * fields are read at negative displacements off `base + index*0x1C`, the END
 * of entry `index`:
 *   -0x10 -> +0x0C uid_high    -0x06 -> +0x16 sequence
 *   -0x0c -> +0x10 uid_low     -0x04 -> +0x18 refcount
 *   -0x08 -> +0x14 next        -0x03 -> +0x19 flags1
 *                              -0x01 -> +0x1B flags2
 * The private biased base this file used to define has been dropped.
 */

/*
 * FILE_$LOCAL_LOCK_VERIFY - Verify local lock ownership
 *
 * Checks if the specified file is locked by the process identified
 * in the request structure.
 *
 * Parameters:
 *   request    - Lock verification request containing file UID and process info
 *   status_ret - Output: status_$ok if locked by process,
 *                        file_$object_not_locked_by_this_process otherwise
 */
void FILE_$LOCAL_LOCK_VERIFY(lock_verify_request_t *request, status_$t *status_ret)
{
    int16_t hash_index;
    int16_t entry_idx;
    file_lock_entry_detail_t *entry;
    int8_t found = 0;

    /*
     * 0x00E60832: `pea (-0x1e0c,PC)` = the word at 0x00E60834 - 0x1E0C =
     * 0x00E5EA28 (image bytes 00 FB = 251), the shared lock-hash modulus cell
     * `file_$lot_hash_modulus`; the second argument is that cell, not nil.
     * Only D0's low word (the remainder) is kept (`move.w D0w,D2w`).
     */
    hash_index = (int16_t)(UID_$HASH(&request->file_uid,
                                     &file_$lot_hash_modulus) & 0xFFFF);

    /* Default status: not locked by this process */
    *status_ret = file_$object_not_locked_by_this_process;

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
        /* Entry `entry_idx` of the 1-based table (0x00E6087A). */
        entry = FILE_$LOT_ENTRY(entry_idx);

        /* Compare UIDs */
        found = 0;
        if (entry->uid_high == request->file_uid.high) {
            if (entry->uid_low == request->file_uid.low) {
                found = -1;  /* UID matches */
            }
        }

        /*
         * Check if lock side matches (bit 7 of flags2)
         */
        uint16_t entry_side = (entry->flags2 >> 7) & 1;
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
            uint16_t entry_mode = (entry->flags2 & 0x78) >> 3;  /* Bits 3-6 = mode */
            uint8_t remote_flag = entry->flags2 & 0x02;  /* Bit 1 = pending/remote */

            if (entry_mode == request->mode) {
                /* 0x00E608B2 `cmp.w (0x12,A2),D0w`: the caller's mode is the
                 * mode this entry already holds. */
                *status_ret = status_$ok;
                goto done;
            }

            if (remote_flag == 0) {
                /*
                 * Not a remote/pending lock - 0x00E608C0-0x00E608C8 maps the
                 * ENTRY's mode through FILE_$LOCK_MODE_MAP and compares the
                 * result with the caller's mode.
                 */
                uint16_t entry_group = FILE_$LOCK_MODE_MAP[entry_mode];
                if (request->mode == entry_group) {
                    *status_ret = status_$ok;
                    goto done;
                }
            }
        }

        /* Move to next entry in chain (entry +0x14, read at (-0x8,An)) */
        entry_idx = (int16_t)entry->next;
    }

done:
    /* Release lock table spinlock */
    ML_$UNLOCK(5);
}
