/*
 * FILE_$IMPORT_LK - Import a lock from another process
 *
 * Original address: 0x00E603AC
 * Size: 130 bytes
 *
 * This function validates a lock index from another process and
 * returns the validated index if the lock exists and matches
 * the specified file UID.
 *
 * Used for inter-process lock sharing/inheritance when a process
 * passes a lock handle to another process.
 *
 * Assembly analysis:
 *   - link.w A6,-0x4        ; Small stack frame
 *   - Validates input index (must be 1-150)
 *   - Looks up lock in per-ASID table for current process
 *   - Verifies UID matches
 *   - Returns same index on success, or error
 */

#include "file/file_internal.h"

/*
 * Both tables are reached through the shared 1-based accessors in
 * file/file_internal.h.  The two base constants this file used to define were
 * the table bases biased down by one element so that `base + index*stride`
 * lands on the 1-based element; the machine holds the unbiased values:
 *   0x00E603FA  movea.l #0xe935cc,A2   -> FILE_$LOT_ENTRY(), entry 1 at 0xE935CC
 *   0x00E603D4  movea.l #0xea202c,A2   -> FILE_$PROC_LOT_SLOT(), row base
 *                                         0xEA202C-0x2662+2 = 0xE9F9CC
 */

/*
 * Maximum lock index per process
 */
#define MAX_LOCK_INDEX          0x96    /* 150 */

/*
 * FILE_$IMPORT_LK - Import a lock from another process
 *
 * Validates that a lock index refers to a valid lock on the
 * specified file for the current process.
 *
 * Parameters:
 *   file_uid   - UID of file the lock should be on
 *   index_in   - Pointer to lock index to validate
 *   index_out  - Output: validated lock index (same as input if valid)
 *   status_ret - Output: status code
 *                status_$ok if valid,
 *                file_$invalid_arg if invalid
 *
 * Note: This function checks that the lock exists in the current
 * process's lock table (PROC1_$AS_ID) and that the file UID matches.
 */
void FILE_$IMPORT_LK(uid_t *file_uid, uint32_t *index_in, uint32_t *index_out,
                      status_$t *status_ret)
{
    uint32_t lock_index = *index_in;
    int16_t entry_idx;
    file_lock_entry_detail_t *entry;

    /*
     * Validate lock index range: must be non-zero and <= 150 (0x96)
     */
    if (lock_index == 0 || lock_index > MAX_LOCK_INDEX) {
        *status_ret = file_$invalid_arg;
        return;
    }

    /*
     * Look up the lock in the per-ASID table for the current process.
     * 0x00E603D4 `movea.l #0xea202c,A2`; the slot is at
     * 0xEA202C + ASID*300 + lock_index*2 - 0x2662, i.e. slot `lock_index` of
     * the 1-based row.
     */
    entry_idx = (int16_t)FILE_$PROC_LOT_SLOT(PROC1_$AS_ID, lock_index);

    if (entry_idx == 0) {
        /* No lock at this index */
        *status_ret = file_$invalid_arg;
        return;
    }

    /*
     * Verify the lock entry matches the requested file UID.
     * 0x00E603FA loads #0xe935cc and the UID is read at (-0x10,An)/(-0xc,An)
     * off the entry END, i.e. fields +0x0C and +0x10 of entry `entry_idx`.
     */
    entry = FILE_$LOT_ENTRY(entry_idx);

    uint32_t entry_uid_high = entry->uid_high;
    uint32_t entry_uid_low = entry->uid_low;

    /* Compare with requested UID */
    if (entry_uid_high != file_uid->high || entry_uid_low != file_uid->low) {
        /* UID mismatch */
        *status_ret = file_$invalid_arg;
        return;
    }

    /*
     * Lock is valid - return the same index
     */
    *status_ret = status_$ok;
    *index_out = lock_index;
}
