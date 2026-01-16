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

/*
 * Lock table base addresses
 */
#define LOT_DATA_BASE       0xE935B0
#define LOT_ENTRY_SIZE      0x1C

/*
 * Per-process lock table (per-ASID)
 * Located at 0xEA202C + ASID*300 + offset
 * Offset 0x1D98 from base+ASID*300 has the lock count
 * Offset 0x2662 is subtracted to get actual lock index array
 */
#define PROC_LOT_TABLE_BASE     0xEA202C
#define PROC_LOT_ENTRY_SIZE     300     /* 0x12C */
#define PROC_LOT_COUNT_OFFSET   0x1D98

/*
 * Boot volume index (from CAL_$BOOT_VOLX)
 * Queries to the boot volume are rejected
 */
extern uint16_t CAL_$BOOT_VOLX;

/*
 * ROUTE_$PORT - Current route/port identifier
 */
extern uint32_t ROUTE_$PORT;

/*
 * DISK_$LVUID_TO_VOLX - Convert logical volume UID to volume index
 *
 * Parameters:
 *   lvuid      - Logical volume UID
 *   volx_table - Output table for volume info
 *
 * Sets status in volx_table[1]
 */
extern void DISK_$LVUID_TO_VOLX(uid_t *lvuid, uint16_t *volx_table);

/*
 * Internal lock entry info structure (expanded format)
 * Size: 34 bytes (0x22)
 *
 * This is the internal format used between FILE_$READ_LOCK_ENTRYI
 * and its callers. Contains more detail than the external format.
 */
typedef struct {
    uid_t    file_uid;      /* 0x00: File UID (8 bytes) */
    uint32_t context;       /* 0x08: Lock context */
    uint32_t owner_node;    /* 0x0C: Owner's node address */
    uint16_t side;          /* 0x10: Lock side (0=reader, 1=writer) */
    uint16_t mode;          /* 0x12: Lock mode (from flags2 bits 3-6) */
    uint16_t sequence;      /* 0x14: Lock sequence number */
    uint32_t holder_node;   /* 0x16: Lock holder's node (NODE_$ME if local) */
    uint32_t holder_port;   /* 0x1A: Lock holder's port (ROUTE_$PORT if local) */
    uint32_t remote_node;   /* 0x1E: Remote node info */
    uint32_t remote_port;   /* 0x22: Remote port info */
} file_lock_info_internal_t;

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
 *   0x000F000C: No more entries (status_$file_no_more_lock_entries)
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
        DISK_$LVUID_TO_VOLX(file_uid, volx_table);
        local_status = volx_table[1];  /* Status returned in second word */

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
     * Compute base address for per-ASID table
     * Table is at 0xEA202C + ASID*300
     */
    int32_t proc_table_base = PROC_LOT_TABLE_BASE + asid * PROC_LOT_ENTRY_SIZE;

    /*
     * Main search loop - may retry if lock holder verification fails
     */
    do {
        local_status = 0x000F000C;  /* status_$file_no_more_lock_entries */
        found_entry = 0;

        ML_$LOCK(5);

        if (is_per_asid < 0) {
            /*
             * Per-ASID table query
             * Lock count is at proc_table_base + 0x1D98
             * Lock indices are at proc_table_base - 0x2662 + index*2
             */
            uint16_t *count_ptr = (uint16_t *)(proc_table_base + PROC_LOT_COUNT_OFFSET);
            uint16_t lock_count = *count_ptr;

            if (start_index <= lock_count) {
                int16_t remaining = lock_count - start_index;
                uint16_t slot = start_index;
                int32_t entry_offset = start_index * 2;

                while (remaining >= 0) {
                    uint16_t *entry_ptr = (uint16_t *)(proc_table_base + entry_offset - 0x2662);
                    if (*entry_ptr != 0) {
                        found_entry = *entry_ptr;
                        start_index = slot + 1;
                        break;
                    }
                    slot++;
                    entry_offset += 2;
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
                uint8_t *entry_ptr = (uint8_t *)(LOT_DATA_BASE + LOT_ENTRY_SIZE +
                                                  entry_idx * LOT_ENTRY_SIZE);

                while (remaining >= 0) {
                    /*
                     * Check if entry is valid (refcount != 0)
                     * and matches volume filter (if volx != 0)
                     */
                    uint8_t *refcount_ptr = entry_ptr - 4;  /* Offset 0x18 - 0x1C = -4 */
                    uint8_t *flags1_ptr = entry_ptr - 3;    /* Offset 0x19 - 0x1C = -3 */
                    uint8_t *flags2_ptr = entry_ptr - 1;    /* Offset 0x1B - 0x1C = -1 */

                    if (*refcount_ptr != 0) {
                        int8_t vol_match = 0;

                        if (volx == 0) {
                            vol_match = -1;  /* No filter - match all */
                        } else if ((*flags2_ptr & 0x04) == 0) {
                            /* Local entry - check volume (stored in flags1 bits 0-5) */
                            uint8_t entry_vol = (*flags1_ptr) & 0x3F;
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
                    entry_ptr += LOT_ENTRY_SIZE;
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
        int32_t entry_offset = found_entry * LOT_ENTRY_SIZE;
        uint8_t *entry_base = (uint8_t *)(LOT_DATA_BASE + LOT_ENTRY_SIZE + entry_offset);

        /* File UID: at offsets -0x10 and -0x0C */
        info_out->file_uid.high = *(uint32_t *)(entry_base - 0x10);
        info_out->file_uid.low = *(uint32_t *)(entry_base - 0x0C);

        /* Lock side: bit 7 of flags2 (at -0x01) */
        info_out->side = (*(entry_base - 0x01) >> 7) & 1;

        /* Lock mode: bits 3-6 of flags2 */
        info_out->mode = ((*(entry_base - 0x01)) & 0x78) >> 3;

        /* Sequence number */
        if (is_per_asid < 0) {
            /* Per-ASID: use refcount byte */
            info_out->sequence = *(entry_base - 4);
        } else {
            /* Global: use sequence field at -0x06 */
            info_out->sequence = *(uint16_t *)(entry_base - 0x06);
        }

        /* Context: at offset -0x1C */
        info_out->context = *(uint32_t *)(entry_base - 0x1C);

        /*
         * Node/port information depends on remote flag (bit 2 of flags2)
         */
        uint8_t remote_flag = (*(entry_base - 0x01)) & 0x04;

        if (remote_flag) {
            /* Remote lock: node info in entry, local node is holder */
            info_out->holder_node = *(uint32_t *)(entry_base - 0x18);
            info_out->holder_port = *(uint32_t *)(entry_base - 0x14);
            info_out->owner_node = NODE_$ME;
            info_out->remote_node = ROUTE_$PORT;
        } else {
            /* Local lock: local node is holder, entry has owner info */
            info_out->holder_node = NODE_$ME;
            info_out->holder_port = ROUTE_$PORT;
            info_out->owner_node = *(uint32_t *)(entry_base - 0x18);
            info_out->remote_node = *(uint32_t *)(entry_base - 0x14);
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
        FILE_$VERIFY_LOCK_HOLDER(&info_out->file_uid, &local_status);

        /*
         * If verification returns "not locked by this process",
         * the lock was released - continue searching
         */
    } while (local_status == status_$file_object_not_locked_by_this_process);

done:
    *status_ret = local_status;

    /* Copy remaining output fields */
    /* (already filled in above, but need to ensure remote_port is set) */
    info_out->remote_port = info_out->remote_node;  /* Same as remote_node for now */

    *index = start_index;
}
