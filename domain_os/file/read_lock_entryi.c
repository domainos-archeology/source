/*
 * FILE_$READ_LOCK_ENTRYI - Read lock entry information (internal)
 *
 * Original address: 0x00E6093C
 * Size: 660 bytes
 *
 * This is the main internal function for reading lock entry information.
 * It handles both local locks (in the main lock table) and per-process
 * locks (in the per-ASID table). It can iterate through all locks on
 * a file or through all locks for a specific volume.
 *
 * Assembly analysis:
 *   - link.w A6,-0x50       ; Large stack frame
 *   - Checks if UID is local vs volume-based
 *   - Uses DISK_$LVUID_TO_VOLX to map volume UIDs
 *   - Iterates through lock table with ML_$LOCK(5) protection
 *   - Calls FILE_$VERIFY_LOCK_HOLDER to verify lock validity
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "disk/disk.h"
#include "cal/cal.h"

/*
 * Lock entries are reached through FILE_$LOT_ENTRY() (file/file_internal.h).
 * 0x00E60A74 and 0x00E60AE0 both load `#0xe935cc` - the table base, entry 1 -
 * and then read fields at negative displacements off `base + index*0x1C`, the
 * END of entry `index`.  The private biased base this file used to define has
 * been dropped in favour of the shared 1-based accessor.
 */

/*
 * The per-process lock table is reached through FILE_$PROC_LOT_SLOT() and
 * FILE_$PROC_LOT_COUNT() (file/file_internal.h).  0x00E609D6 and 0x00E60A06
 * load `#0xea202c`; slot `i` of row `asid` is at 0xEA202C + asid*300 + i*2
 * - 0x2662 (row base 0xE9F9CC, 1-based) and the row's slot high-water mark is
 * at 0xEA202C + asid*300 + 0x1D98 (0xEA3DC4 + asid*2).
 */

/* CAL_$BOOT_VOLX is declared in cal/cal.h */

/* ROUTE_$PORT is declared in route/route.h */

/* DISK_$LVUID_TO_VOLX and file_lock_info_internal_t are declared in headers */

/*
 * FILE_$READ_LOCK_ENTRYI - Read lock entry information (internal)
 *
 * Iterates through lock entries for a file or volume.
 *
 * Parameters:
 *   file_uid   - File/volume UID to query. Format determines behavior:
 *                - byte[0]=0, byte[1]=1, short[1]>=0: Per-ASID table (ASID in short[1])
 *                - Otherwise: Global lock table by volume
 *   index      - Pointer to iteration index:
 *                - Input: Starting index (0 means start at 1)
 *                - Output: Next index to use, or 0xFFFF if done
 *   info_out   - Output buffer for lock info (34 bytes)
 *   status_ret - Output: status code
 *
 * Status codes:
 *   status_$ok: Entry found and returned
 *   0x000F000C: No more entries (file_$no_more_lock_entries)
 *   0x00140002: Query not allowed (boot volume)
 */
void FILE_$READ_LOCK_ENTRYI(uid_t *file_uid, uint16_t *index,
                             file_lock_info_internal_t *info_out,
                             status_$t *status_ret)
{
    uint32_t uid_high = file_uid->high;
    uint32_t uid_low = file_uid->low;
    int8_t is_per_asid = 0;  /* True if querying per-ASID table */
    uint16_t start_index;
    uint16_t found_entry = 0;
    uint16_t volx = 0;
    int16_t asid = 0;
    status_$t local_status;
    uint16_t volx_table[3];
    uint8_t byte0, byte1;
    int16_t short1;

    /* Start index: if 0 passed, start at 1 */
    start_index = *index;
    if (start_index == 0) {
        start_index = 1;
    }

    /*
     * Check first byte of UID to determine query type
     */
    byte0 = (uint8_t)(uid_high >> 24);

    if (byte0 == 0) {
        /*
         * First byte is 0 - check for per-ASID table query
         * Per-ASID: byte[0]=0, byte[1]=1, short[1] (bytes 2-3) >= 0 and < 0x3A
         */
        short1 = (int16_t)(uid_high & 0xFFFF);  /* Bytes 2-3 as signed short */
        byte1 = (uint8_t)(uid_high >> 16);      /* Byte 1 */

        if (short1 < 0x3A && byte1 == 0x01 && short1 >= 0) {
            is_per_asid = -1;  /* Per-ASID table query */
            asid = short1;
        }
    } else {
        /*
         * Non-zero first byte: Map volume UID to volume index
         */
        DISK_$LVUID_TO_VOLX(file_uid, (int16_t *)&volx_table[0], &local_status);

        if (local_status != 0) {
            *status_ret = local_status;
            return;
        }

        volx = volx_table[0];

        /* Reject queries to boot volume */
        if (volx == CAL_$BOOT_VOLX) {
            *status_ret = 0x140002;  /* Query not allowed */
            return;
        }
    }

    /*
     * Main search loop - may retry if lock holder verification fails
     */
    do {
        local_status = 0x000F000C;  /* file_$no_more_lock_entries */
        found_entry = 0;

        ML_$LOCK(5);

        if (is_per_asid < 0) {
            /*
             * Per-ASID table query: walk slots start_index..lock_count of the
             * 1-based row for `asid` (0x00E609D6 / 0x00E60A06).
             */
            uint16_t lock_count = FILE_$PROC_LOT_COUNT(asid);

            if (start_index <= lock_count) {
                int16_t remaining = lock_count - start_index;
                uint16_t slot = start_index;

                while (remaining >= 0) {
                    uint16_t slot_value = FILE_$PROC_LOT_SLOT(asid, slot);
                    if (slot_value != 0) {
                        found_entry = slot_value;
                        start_index = slot + 1;
                        break;
                    }
                    slot++;
                    remaining--;
                }
            }
        } else {
            /*
             * Global lock table query (by volume)
             * Iterate through all entries checking volume match
             */
            if (start_index <= FILE_$LOT_HIGH) {
                int16_t remaining = FILE_$LOT_HIGH - start_index;
                uint16_t entry_idx = start_index;
                file_lock_entry_detail_t *scan = FILE_$LOT_ENTRY(entry_idx);

                while (remaining >= 0) {
                    /*
                     * Check if entry is valid (refcount != 0)
                     * and matches volume filter (if volx != 0).
                     * refcount is +0x18 ((-0x4,An)), flags1 +0x19 ((-0x3,An))
                     * and flags2 +0x1B ((-0x1,An)) off the entry end.
                     */
                    if (scan->refcount != 0) {
                        int8_t vol_match = 0;

                        if (volx == 0) {
                            vol_match = -1;  /* No filter - match all */
                        } else if ((scan->flags2 & 0x04) == 0) {
                            /* Local entry - check volume (stored in flags1 bits 0-5) */
                            uint8_t entry_vol = scan->flags1 & 0x3F;
                            if (entry_vol == volx) {
                                vol_match = -1;
                            }
                        }

                        if (vol_match < 0) {
                            found_entry = entry_idx;
                            start_index = entry_idx + 1;
                            break;
                        }
                    }

                    entry_idx++;
                    scan++;
                    remaining--;
                }
            }
        }

        /*
         * If no entry found, we're done
         */
        if (found_entry == 0) {
            ML_$UNLOCK(5);
            start_index = 0xFFFF;
            goto done;
        }

        /*
         * Found an entry - extract information
         */
        file_lock_entry_detail_t *entry = FILE_$LOT_ENTRY(found_entry);

        /* File UID: +0x0C and +0x10, read at (-0x10,An)/(-0xc,An) */
        info_out->file_uid.high = entry->uid_high;
        info_out->file_uid.low = entry->uid_low;

        /* Lock side: bit 7 of flags2 (+0x1B, (-0x1,An)) */
        info_out->side = (entry->flags2 >> 7) & 1;

        /* Lock mode: bits 3-6 of flags2 */
        info_out->mode = (entry->flags2 & 0x78) >> 3;

        /* Sequence number */
        if (is_per_asid < 0) {
            /* Per-ASID: use refcount byte (+0x18, (-0x4,An)) */
            info_out->sequence = entry->refcount;
        } else {
            /* Global: use sequence field (+0x16, (-0x6,An)) */
            info_out->sequence = entry->sequence;
        }

        /* Context: +0x00, read at (-0x1c,An) */
        info_out->context = entry->context;

        /*
         * Node/port information depends on remote flag (bit 2 of flags2)
         */
        uint8_t remote_flag = entry->flags2 & 0x04;

        if (remote_flag) {
            /*
             * Remote lock: entry has remote holder info, local is owner
             * holder_node/port = entry.node_low/high (remote holder)
             * owner_node = NODE_$ME (we are the owner)
             * remote_info = ROUTE_$PORT
             */
            info_out->holder_node = entry->node_low;
            info_out->holder_port = entry->node_high;
            info_out->owner_node = NODE_$ME;
            info_out->remote_info = ROUTE_$PORT;
        } else {
            /*
             * Local lock: local node is holder
             * holder_node/port = NODE_$ME/ROUTE_$PORT (we are the holder)
             * owner_node = entry.node_low (who locked it)
             * remote_info = entry.node_high
             */
            info_out->holder_node = NODE_$ME;
            info_out->holder_port = ROUTE_$PORT;
            info_out->owner_node = entry->node_low;
            info_out->remote_info = entry->node_high;
        }

        ML_$UNLOCK(5);

        /*
         * For global queries with non-zero uid_low, verify lock holder
         * Skip verification for per-ASID queries (uid_low is always the ASID pattern)
         */
        if (byte0 == 0 && uid_low != 0) {
            /* Per-ASID query with specific UID - no verification needed */
            goto done;
        }

        /* Verify lock holder is still valid */
        FILE_$VERIFY_LOCK_HOLDER(info_out, &local_status);

        /*
         * If verification returns "not locked by this process",
         * the lock was released - continue searching
         */
    } while (local_status == file_$object_not_locked_by_this_process);

done:
    *status_ret = local_status;
    *index = start_index;
}
