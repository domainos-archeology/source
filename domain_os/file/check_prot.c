/*
 * FILE_$CHECK_PROT - Check file protection/access rights
 *
 * Checks if the current process has the requested access rights to a file.
 * First checks a per-process lock cache, then falls back to ACL_$RIGHTS.
 *
 * Original address: 0x00E5D172
 */

#include "file/file_internal.h"

/*
 * Lock table addresses (m68k)
 *
 * The lock lookup table is organized as:
 * - Base at 0xEA202C (FILE_$LOCK_TABLE2 + offset)
 * - Indexed by PROC1_$AS_ID * 300 + slot * 2
 * - Each entry is a 16-bit index into the lock entries table
 *
 * Lock entries are at 0xE935BC with the following layout per entry:
 * - Offset 0x0C: UID high (4 bytes)
 * - Offset 0x10: UID low (4 bytes)
 * - Offset 0x1A: Flags byte
 * - Entry size: 28 bytes (0x1C)
 *
 * Lock flags (bit meanings):
 * - Bit 4 (0x10): Entry is invalid/placeholder
 * - Other bits: Access rights mask
 */

/* Lock lookup table base (indexed by AS_ID and slot) */
#define LOCK_LOOKUP_TABLE_BASE      0xEA202C
#define LOCK_LOOKUP_OFFSET          0x2662   /* Offset from table to first entry */
#define LOCK_ENTRIES_PER_ASID       300      /* Entries per address space */
#define MAX_LOCK_SLOT               0x96     /* Maximum slot value (150) */

/* Lock entry offsets from entry base */
#define LOCK_ENTRY_SIZE             28       /* 0x1C bytes per entry */
#define LOCK_ENTRY_UID_HIGH_OFF     0x0C
#define LOCK_ENTRY_UID_LOW_OFF      0x10
#define LOCK_ENTRY_FLAGS_OFF        0x1A

/* Lock entry base address */
#define LOCK_ENTRIES_BASE           0xE935BC

/* Status codes */
#define status_$no_right_to_perform_operation               0x00230001
#define status_$insufficient_rights_to_perform_operation    0x00230002

/*
 * FILE_$CHECK_PROT
 *
 * Checks protection rights for a file. Uses a fast path through the
 * per-process lock cache when available, otherwise falls back to
 * full ACL checking via ACL_$RIGHTS.
 *
 * Parameters:
 *   file_uid     - UID of file to check (8 bytes)
 *   access_mask  - Required access rights mask
 *   slot_num     - Lock table slot number (0-149)
 *   unused       - Unused parameter
 *   rights_out   - Output: actual rights available
 *   status_ret   - Output: status code
 *
 * Returns:
 *   1 if rights check completed (check status for success/failure)
 *   Result from ACL_$RIGHTS otherwise
 *
 * Flow:
 * 1. If slot_num is valid (1-149):
 *    a. Look up entry in per-AS lock table
 *    b. If entry found and UID matches and entry is valid:
 *       - Return rights from cache
 * 2. Otherwise call ACL_$RIGHTS for full access check
 */
int16_t FILE_$CHECK_PROT(uid_t *file_uid, uint16_t access_mask, uint32_t slot_num,
                         void *unused, uint16_t *rights_out, status_$t *status_ret)
{
    int16_t lock_index;
    int32_t entry_offset;
    uint8_t entry_flags;
    uint32_t *entry_ptr;
    uint32_t rights_mask;

    *status_ret = status_$ok;

    /*
     * Check if slot_num is valid for cache lookup.
     * Slot 0 is not used, and max is 149 (0x95).
     */
    if (slot_num != 0 && slot_num < MAX_LOCK_SLOT) {
        /*
         * Calculate address in lock lookup table:
         * table_addr = LOCK_LOOKUP_TABLE_BASE + PROC1_$AS_ID * 300 + slot_num * 2 - LOCK_LOOKUP_OFFSET
         *
         * The lookup table contains 16-bit indices into the lock entries array.
         */
#ifdef M68K_TARGET
        int16_t *lookup_table = (int16_t *)(LOCK_LOOKUP_TABLE_BASE +
                                            (int16_t)(PROC1_$AS_ID * LOCK_ENTRIES_PER_ASID) +
                                            slot_num * 2 - LOCK_LOOKUP_OFFSET);
        lock_index = *lookup_table;
#else
        /* For non-m68k targets, this would need to access emulated memory */
        lock_index = 0;
#endif

        if (lock_index != 0) {
            /*
             * Found an entry. Calculate the offset into lock entries table.
             * Each lock entry is 28 bytes.
             */
            entry_offset = (int32_t)lock_index * LOCK_ENTRY_SIZE;

#ifdef M68K_TARGET
            /* Get entry flags */
            entry_flags = *(uint8_t *)(LOCK_ENTRIES_BASE + entry_offset + LOCK_ENTRY_FLAGS_OFF - LOCK_ENTRY_UID_HIGH_OFF);

            /* Return current rights */
            *rights_out = (uint16_t)entry_flags;

            /* Get entry UID and compare */
            entry_ptr = (uint32_t *)(LOCK_ENTRIES_BASE + entry_offset);

            /* Check if UID matches and entry is valid (bit 4 not set) */
            if (entry_ptr[0] == file_uid->high &&
                entry_ptr[1] == file_uid->low &&
                (entry_flags & 0x10) == 0) {

                /* If no rights at all, return error */
                if (entry_flags == 0) {
                    *status_ret = status_$no_right_to_perform_operation;
                    return 1;
                }

                /* Check if we have the required rights */
                if ((access_mask & entry_flags) == access_mask) {
                    /* Have sufficient rights */
                    return 1;
                }

                /* Insufficient rights */
                *status_ret = status_$insufficient_rights_to_perform_operation;
                return 1;
            }
#else
            (void)entry_offset;
            (void)entry_flags;
            (void)entry_ptr;
#endif
        }
    }

    /*
     * Cache miss or invalid slot - perform full ACL check.
     * ACL_$RIGHTS takes:
     *   - file_uid
     *   - unused parameter (we pass &unused to match signature)
     *   - pointer to access mask
     *   - pointer to option flags (we use the high word of unused)
     *   - status_ret
     */
    rights_mask = (uint32_t)access_mask;
    *rights_out = ACL_$RIGHTS(file_uid, unused, &rights_mask,
                              (int16_t *)((uint8_t *)&unused + 2), status_ret);

    return (int16_t)*rights_out;
}
