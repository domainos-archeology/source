/*
 * FILE_$PRIV_UNLOCK_ALL - Unlock all locks for a process
 *
 * Original address: 0x00E60BD0
 * Size: 358 bytes
 *
 * Releases all locks held by one or all processes. Used during
 * process termination or when cleaning up all locks.
 *
 * Parameters:
 *   asid_ptr - Pointer to ASID (0 = all processes, 1-57 = specific process)
 *
 * Assembly analysis:
 *   - If *asid_ptr == 0, processes ASIDs 0-57 (0x39)
 *   - For each process, iterates through lock table entries
 *   - For entries with refcount >= 2, just decrements and clears slot
 *   - For entries with refcount < 2, calls FILE_$PRIV_UNLOCK
 *   - Finally calls REM_FILE_$UNLOCK_ALL if *asid_ptr == 0
 */

#include "file/file_internal.h"
#include "ml/ml.h"

/*
 * Lock table base addresses (m68k target)
 */
#define LOT_BASE        ((file_lock_entry_detail_t *)0xE935B0)
#define LOT_ENTRY(n)    ((file_lock_entry_detail_t *)((uint8_t *)LOT_BASE + (n) * 0x1C))

/* Per-process lock table */
#define PROC_LOT_BASE   ((uint16_t *)0xE9F9CA)
#define PROC_LOT_ENTRY(asid, idx) \
    (*(uint16_t *)((uint8_t *)PROC_LOT_BASE + (asid) * 300 + (idx) * 2))
#define PROC_LOT_COUNT(asid) \
    (*(uint16_t *)((uint8_t *)0xEA3DC4 + (asid) * 2))

/*
 * FILE_$PRIV_UNLOCK_ALL - Unlock all locks for a process
 */
void FILE_$PRIV_UNLOCK_ALL(uint16_t *asid_ptr)
{
    uint16_t start_asid, end_asid;
    uint16_t asid;
    int16_t count;
    uint16_t slot;
    uint16_t entry_idx;
    file_lock_entry_detail_t *entry;
    uid_t local_uid;
    uint32_t dtv_out[2];
    status_$t local_status;

    /*
     * Determine range of ASIDs to process
     */
    if (*asid_ptr == 0) {
        /* Process all ASIDs (0-57) */
        start_asid = 0;
        end_asid = 0x39;  /* 57 */
    } else {
        /* Process single ASID */
        start_asid = *asid_ptr;
        end_asid = *asid_ptr;
    }

    ML_$LOCK(5);

    /*
     * Iterate through each ASID in range
     */
    for (asid = start_asid; asid <= end_asid; asid++) {
        count = PROC_LOT_COUNT(asid) - 1;
        if (count >= 0) {
            /*
             * Iterate through all slots for this ASID
             */
            for (slot = 1; slot <= (uint16_t)(count + 1); slot++) {
                entry_idx = PROC_LOT_ENTRY(asid, slot);
                if (entry_idx != 0) {
                    entry = LOT_ENTRY(entry_idx);

                    /*
                     * Check refcount to decide how to handle
                     */
                    if (entry->refcount >= 2) {
                        /*
                         * Multiple references - just decrement and clear slot
                         * Don't actually unlock yet
                         */
                        PROC_LOT_ENTRY(asid, slot) = 0;
                        entry->refcount--;
                    } else {
                        /*
                         * Single reference - need to fully unlock
                         * Save UID before unlock releases the entry
                         */
                        local_uid.high = entry->uid_high;
                        local_uid.low = entry->uid_low;

                        ML_$UNLOCK(5);

                        /*
                         * Call FILE_$PRIV_UNLOCK for this entry
                         * Pass lock mode 0 to match any, and the slot number
                         */
                        FILE_$PRIV_UNLOCK(&local_uid,
                                          (uint16_t)slot,
                                          (uint32_t)asid,  /* mode=0, asid in low word */
                                          0,               /* remote_flags */
                                          0,               /* param_5 */
                                          0,               /* param_6 */
                                          dtv_out,         /* dtv_out */
                                          &local_status);

                        ML_$LOCK(5);
                    }
                }
            }
        }

        /*
         * Clear the count for this ASID
         */
        PROC_LOT_COUNT(asid) = 0;
    }

    ML_$UNLOCK(5);

    /*
     * If unlocking all processes, also unlock all remote locks
     */
    if (*asid_ptr == 0) {
        REM_FILE_$UNLOCK_ALL();
    }
}
