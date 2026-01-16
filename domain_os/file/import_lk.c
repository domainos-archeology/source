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
 * Lock table base addresses
 */
#define LOT_DATA_BASE           0xE935B0
#define LOT_ENTRY_SIZE          0x1C

/*
 * Per-process lock table
 * Base at 0xEA202C + ASID*300
 * Lock indices at base + index*2 - 0x2662
 */
#define PROC_LOT_TABLE_BASE     0xEA202C
#define PROC_LOT_ENTRY_SIZE     300     /* 0x12C */
#define PROC_LOT_INDEX_OFFSET   (-0x2662)

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
 *                status_$file_invalid_arg if invalid
 *
 * Note: This function checks that the lock exists in the current
 * process's lock table (PROC1_$AS_ID) and that the file UID matches.
 */
void FILE_$IMPORT_LK(uid_t *file_uid, uint32_t *index_in, uint32_t *index_out,
                      status_$t *status_ret)
{
    uint32_t lock_index = *index_in;
    int16_t entry_idx;
    int32_t entry_offset;
    uint8_t *entry_base;

    /*
     * Validate lock index range: must be non-zero and <= 150 (0x96)
     */
    if (lock_index == 0 || lock_index > MAX_LOCK_INDEX) {
        *status_ret = status_$file_invalid_arg;
        return;
    }

    /*
     * Look up the lock in the per-ASID table for current process
     * Table base = 0xEA202C + PROC1_$AS_ID * 300
     * Entry = base + lock_index * 2 - 0x2662
     */
    int32_t proc_table_base = PROC_LOT_TABLE_BASE + PROC1_$AS_ID * PROC_LOT_ENTRY_SIZE;
    uint16_t *entry_ptr = (uint16_t *)(proc_table_base + lock_index * 2 + PROC_LOT_INDEX_OFFSET);

    entry_idx = *entry_ptr;

    if (entry_idx == 0) {
        /* No lock at this index */
        *status_ret = status_$file_invalid_arg;
        return;
    }

    /*
     * Verify the lock entry matches the requested file UID
     */
    entry_offset = (int32_t)entry_idx * LOT_ENTRY_SIZE;
    entry_base = (uint8_t *)(LOT_DATA_BASE + LOT_ENTRY_SIZE + entry_offset);

    /* Get file UID from entry (at offsets -0x10 and -0x0C) */
    uint32_t entry_uid_high = *(uint32_t *)(entry_base - 0x10);
    uint32_t entry_uid_low = *(uint32_t *)(entry_base - 0x0C);

    /* Compare with requested UID */
    if (entry_uid_high != file_uid->high || entry_uid_low != file_uid->low) {
        /* UID mismatch */
        *status_ret = status_$file_invalid_arg;
        return;
    }

    /*
     * Lock is valid - return the same index
     */
    *status_ret = status_$ok;
    *index_out = lock_index;
}
