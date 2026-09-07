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
 * The lock tables are addressed through FILE_$LOT_ENTRY / FILE_$PROC_LOT_SLOT /
 * FILE_$PROC_LOT_COUNT (file/file_internal.h).  This file used to carry a
 * private lock-table base biased down by one entry so that `base + index*0x1C`
 * lands on the 1-based entry; the machine never holds that value.
 * 0x00E60C70 loads `#0xe935cc` (the real base, entry 1) and 0x00E60C0C /
 * 0x00E60C22 load `#0xea202c` for the per-process rows.
 */

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
        count = FILE_$PROC_LOT_COUNT(asid) - 1;
        if (count >= 0) {
            /*
             * Iterate through all slots for this ASID
             */
            for (slot = 1; slot <= (uint16_t)(count + 1); slot++) {
                entry_idx = FILE_$PROC_LOT_SLOT(asid, slot);
                if (entry_idx != 0) {
                    entry = FILE_$LOT_ENTRY(entry_idx);

                    /*
                     * Check refcount to decide how to handle
                     */
                    if (entry->refcount >= 2) {
                        /*
                         * Multiple references - just decrement and clear slot
                         * Don't actually unlock yet
                         */
                        FILE_$PROC_LOT_SLOT(asid, slot) = 0;
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
                         * 0x00E60CB0-0x00E60CCC, pushed right to left:
                         *   pea (-0x14,A6)      status_ret = &local_status
                         *   pea (-0x10,A6)      dtv_out
                         *   clr.l / clr.l       rem_node, rem_key
                         *   clr.l               by_key = false, key = 0
                         *   move.w D3w          asid
                         *   clr.w               lock_mode = 0 (any)
                         *   clr.l D1; move.w D5w,D1w; move.l D1
                         *                       lock_slot, zero-extended
                         *   pea (-0x8,A6)       file_uid = &local_uid
                         */
                        (void)FILE_$PRIV_UNLOCK(&local_uid,
                                                (int32_t)(uint16_t)slot, /* lock_slot */
                                                0,              /* lock_mode: any */
                                                (uint16_t)asid, /* asid           */
                                                0,              /* by_key         */
                                                0,              /* key            */
                                                0,              /* rem_key        */
                                                0,              /* rem_node       */
                                                dtv_out,
                                                &local_status);

                        ML_$LOCK(5);
                    }
                }
            }
        }

        /*
         * Clear the count for this ASID
         */
        FILE_$PROC_LOT_COUNT(asid) = 0;
    }

    ML_$UNLOCK(5);

    /*
     * If unlocking all processes, also unlock all remote locks
     */
    if (*asid_ptr == 0) {
        REM_FILE_$UNLOCK_ALL();
    }
}
