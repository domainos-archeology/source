/*
 * FILE_$EXPORT_LK - Export a lock to another process
 *
 * Original address: 0x00E74110
 * Size: 306 bytes
 *
 * Exports a file lock from the current process to another process,
 * allowing the target process to share the lock. This is used for
 * lock inheritance across process boundaries (e.g., fork).
 *
 * Assembly analysis:
 *   - link.w A6,-0x10         ; Stack frame
 *   - Saves D2-D5, A2, A3, A5 to stack
 *   - Saves current ASID from PROC1_$AS_ID to D2
 *   - Calls PROC2_$FIND_ASID to get target process ASID
 *   - Validates lock_index is in range [1, 0x96]
 *   - Looks up lock entry in per-process table
 *   - Verifies file UID matches
 *   - Acquires lock 5 via ML_$LOCK
 *   - Searches for free slot in target's lock table
 *   - If found, copies lock index, updates count, increments refcount
 *   - Releases lock 5 via ML_$UNLOCK
 *
 * Data structures used:
 *   - Per-process lock table at 0xEA202C + ASID*300
 *   - Lock entry data at 0xE935BC + entry*0x1C
 *   - Max lock count at 0xEA3DC4 + ASID*2
 */

#include "file/file_internal.h"
#include "proc2/proc2.h"

/* Constants from assembly */
#define MAX_PROC_LOCKS      150     /* 0x96 */
#define LOCK_ENTRY_SIZE     0x1C    /* 28 bytes */
#define PROC_LOT_BASE       0xEA202C
#define LOT_DATA_BASE       0xE935BC
#define LOT_MAX_COUNT_BASE  0xEA3DC4
#define PROC_LOT_OFFSET     (-0x2662)

/* Dummy parameter for PROC2_$FIND_ASID */
static const int8_t find_asid_param = 0;

/*
 * FILE_$EXPORT_LK - Export a lock to another process
 *
 * Exports a file lock held by the current process to another process
 * identified by process UID. This allows the target process to share
 * the lock, incrementing the lock's reference count.
 *
 * Parameters:
 *   file_uid    - UID of file that must match the locked file
 *   lock_index  - Pointer to lock index to export
 *   target_proc - UID of target process to export lock to
 *   index_out   - Output: lock index assigned in target's table
 *   status_ret  - Output status code
 *
 * The function:
 *   1. Finds the target process's ASID
 *   2. Validates the lock index is in valid range
 *   3. Looks up the lock entry and verifies the file UID matches
 *   4. Finds a free slot in the target's lock table
 *   5. Copies the lock reference and increments the refcount
 *
 * Status codes:
 *   status_$ok                              - Export succeeded
 *   status_$proc2_uid_not_found             - Target process not found
 *   status_$file_invalid_arg                - Invalid lock index
 *   status_$file_object_not_locked_by_this_process - Lock not found or UID mismatch
 *   status_$file_local_lock_table_full      - Target's lock table is full
 */
void FILE_$EXPORT_LK(uid_t *file_uid, uint32_t *lock_index,
                     uid_t *target_proc, int32_t *index_out,
                     status_$t *status_ret)
{
    int16_t current_asid;
    int16_t target_asid;
    uint32_t idx;
    int16_t entry_idx;
    uint8_t *entry_ptr;
    uint32_t entry_offset;
    int16_t slot;
    int16_t remaining;
    int32_t proc_table_base;

    /* Save current process ASID */
    current_asid = PROC1_$AS_ID;

    /* Find target process ASID */
    target_asid = PROC2_$FIND_ASID(target_proc, (int8_t *)&find_asid_param, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Validate lock index is in range [1, 0x96] */
    idx = *lock_index;
    if (idx == 0 || idx > MAX_PROC_LOCKS) {
        *status_ret = status_$file_invalid_arg;
        return;
    }

    /*
     * Look up lock entry in current process's table
     * Table is at PROC_LOT_BASE + ASID*300 + PROC_LOT_OFFSET + index*2
     */
    proc_table_base = PROC_LOT_BASE + current_asid * FILE_PROC_LOCK_ENTRY_SIZE;
    entry_idx = *(int16_t *)(proc_table_base + PROC_LOT_OFFSET + idx * 2);

    if (entry_idx == 0) {
        *status_ret = status_$file_object_not_locked_by_this_process;
        return;
    }

    /* Get lock entry data */
    entry_offset = entry_idx * LOCK_ENTRY_SIZE;
    entry_ptr = (uint8_t *)(LOT_DATA_BASE + entry_offset);

    /* Check refcount is non-zero */
    if (entry_ptr[0x0C] == 0) {  /* refcount at offset 0x0C from entry base */
        *status_ret = status_$file_object_not_locked_by_this_process;
        return;
    }

    /* Verify file UID matches */
    uint32_t *entry_uid = (uint32_t *)(entry_ptr);
    if (entry_uid[0] != file_uid->high || entry_uid[1] != file_uid->low) {
        *status_ret = status_$file_object_not_locked_by_this_process;
        return;
    }

    /* Set initial status - will be cleared if slot found */
    *status_ret = status_$file_local_lock_table_full;

    /* Acquire lock 5 (file locking lock) */
    ML_$LOCK(5);

    /*
     * Search for free slot in target's lock table
     * Iterate through slots 1 to 0x96 (150 entries)
     */
    proc_table_base = PROC_LOT_BASE + target_asid * FILE_PROC_LOCK_ENTRY_SIZE;

    remaining = MAX_PROC_LOCKS - 1;  /* 0x95 */
    slot = 1;

    while (remaining >= 0) {
        int16_t *slot_ptr = (int16_t *)(proc_table_base + PROC_LOT_OFFSET + slot * 2);

        if (*slot_ptr == 0) {
            /* Found free slot - assign lock index */
            *slot_ptr = entry_idx;

            /* Update max lock count if needed */
            int16_t *max_ptr = (int16_t *)(LOT_MAX_COUNT_BASE + target_asid * 2);
            if (*max_ptr < slot) {
                *max_ptr = slot;
            }

            /* Increment lock refcount */
            entry_ptr[0x0C]++;

            /* Success */
            *status_ret = status_$ok;
            *index_out = (int32_t)slot;
            break;
        }

        slot++;
        remaining--;
    }

    /* Release lock 5 */
    ML_$UNLOCK(5);
}
