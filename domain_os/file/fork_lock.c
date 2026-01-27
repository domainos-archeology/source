/*
 * FILE_$FORK_LOCK - Duplicate parent's file lock table to child during fork
 *
 * Original address: 0x00E74244
 * Size: 204 bytes
 *
 * During a fork, the child process inherits the parent's file locks.
 * This function copies all lock table entries from the parent's per-process
 * lock table to the child's table, incrementing each lock entry's reference
 * count.
 *
 * Assembly analysis:
 *   - link.w A6,-0x10         ; Stack frame (16 bytes local)
 *   - Saves D2-D7, A2-A5 to stack
 *   - D2w = PROC1_$AS_ID (current/parent ASID)
 *   - D4 = param_1 (pointer to child's ASID)
 *   - Acquires ML_$LOCK(5)
 *   - Reads slot count from FILE_$LOCK_TABLE2[parent_asid]
 *   - Iterates slots 1..count in parent's per-process lock table
 *   - For each non-zero entry: copies to child's table, increments refcount
 *   - Copies slot count to child's FILE_$LOCK_TABLE2[child_asid]
 *   - Releases ML_$UNLOCK(5)
 *
 * Data structures used:
 *   - Per-process lock table at 0xE9F9CA + ASID*300 + slot*2
 *     (= PROC_LOT_BASE + PROC_LOT_OFFSET + ASID*300 + slot*2)
 *   - Lock entry refcount at 0xE935C8 + entry*0x1C
 *     (= LOT_DATA_BASE + 0x0C + entry*LOCK_ENTRY_SIZE)
 *   - Per-process slot count at 0xEA3DC4 + ASID*2
 *     (= LOT_MAX_COUNT_BASE + ASID*2 = PROC_LOT_BASE + 0x1D98 + ASID*2)
 */

#include "file/file_internal.h"

/* Constants from assembly (same as export_lk.c) */
#define MAX_PROC_LOCKS      150     /* 0x96 */
#define LOCK_ENTRY_SIZE     0x1C    /* 28 bytes */
#define PROC_LOT_BASE       0xEA202C
#define LOT_DATA_BASE       0xE935BC
#define LOT_MAX_COUNT_BASE  0xEA3DC4
#define PROC_LOT_OFFSET     (-0x2662)

/*
 * FILE_$FORK_LOCK - Duplicate parent's file lock table to child process
 *
 * Called during fork (from PROC2_$FORK) to inherit the parent process's
 * file locks into the child process. Each lock entry's reference count
 * is incremented to reflect the additional holder.
 *
 * Parameters:
 *   new_asid   - Pointer to child process's ASID (uint16_t)
 *   status_ret - Output status code (always set to status_$ok)
 *
 * Note: The original Pascal code likely passed the ASID as a var parameter.
 * The caller (PROC2_$FORK) passes &new_entry->asid.
 */
void FILE_$FORK_LOCK(uint16_t *new_asid, status_$t *status_ret)
{
    int16_t parent_asid;
    int16_t child_asid;
    int16_t slot_count;
    int16_t i;
    uint16_t entry_idx;
    int32_t parent_table_base;
    int32_t child_table_base;

    /* Save parent process ASID */
    parent_asid = PROC1_$AS_ID;

    /* Set status to success */
    *status_ret = status_$ok;

    /* Acquire mutex lock #5 (file locking) */
    ML_$LOCK(5);

    /*
     * Read parent's slot count from FILE_$LOCK_TABLE2[parent_asid]
     * Assembly: tst.w (0x1d98,A1) where A1 = PROC_LOT_BASE + parent_asid*2
     * This is equivalent to *(LOT_MAX_COUNT_BASE + parent_asid*2)
     */
    slot_count = *(int16_t *)(LOT_MAX_COUNT_BASE + parent_asid * 2);

    if (slot_count != 0) {
        /*
         * Compute base addresses for parent's per-process lock table.
         * Assembly: per-process table at 0xE9F9CA + ASID*300 + slot*2
         * which is PROC_LOT_BASE + PROC_LOT_OFFSET + ASID*300 + slot*2
         */
        parent_table_base = PROC_LOT_BASE + PROC_LOT_OFFSET
                            + (int16_t)(parent_asid * FILE_PROC_LOCK_ENTRY_SIZE);

        child_asid = *new_asid;
        child_table_base = PROC_LOT_BASE + PROC_LOT_OFFSET
                           + (int16_t)(child_asid * FILE_PROC_LOCK_ENTRY_SIZE);

        /*
         * Iterate through slots 1..slot_count
         * Assembly: D0w = slot_count - 1, iVar5(D1) starts at 2, increments by 2
         * dbf D0w loops slot_count times (slots 1 through slot_count)
         * Slot offset = slot * 2, starting at offset 2 (slot 1)
         */
        for (i = 1; i <= slot_count; i++) {
            entry_idx = *(uint16_t *)(parent_table_base + i * 2);

            if (entry_idx != 0) {
                /* Copy lock entry index to child's table at same slot */
                *(uint16_t *)(child_table_base + i * 2) = entry_idx;

                /*
                 * Increment lock entry's reference count
                 * Refcount is at LOT_DATA_BASE + entry*LOCK_ENTRY_SIZE + 0x0C
                 * Assembly: (&DAT_00e935c8)[(uint)uVar1 * 0x1c] += 1
                 * where 0xE935C8 = LOT_DATA_BASE + 0x0C
                 */
                uint8_t *refcount_ptr = (uint8_t *)(LOT_DATA_BASE
                                        + (uint32_t)entry_idx * LOCK_ENTRY_SIZE
                                        + 0x0C);
                (*refcount_ptr)++;
            }
        }
    }

    /*
     * Copy slot count from parent to child:
     * FILE_$LOCK_TABLE2[child_asid] = FILE_$LOCK_TABLE2[parent_asid]
     *
     * Assembly: move.w (0x1d98,A2),(0x1d98,A0)
     * where A2 = PROC_LOT_BASE + parent_asid*2
     *   and A0 = PROC_LOT_BASE + child_asid*2
     * Note: *new_asid is re-read from the pointer here (matching assembly:
     *   movea.l D4,A1; move.w (A1),D0w)
     */
    child_asid = *new_asid;
    *(int16_t *)(LOT_MAX_COUNT_BASE + child_asid * 2) =
        *(int16_t *)(LOT_MAX_COUNT_BASE + parent_asid * 2);

    /* Release mutex lock #5 */
    ML_$UNLOCK(5);
}
