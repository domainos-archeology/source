/*
 * FILE_$LOCK_INIT - Initialize file locking subsystem
 *
 * Original address: 0x00E32744
 *
 * This function initializes all data structures used for file locking:
 *
 * 1. Lock table (58 entries × 300 bytes starting at 0xE9F9CC):
 *    - Each entry is a hash bucket for file locks keyed by UID
 *    - First 2 bytes preserved, remaining 298 bytes cleared
 *
 * 2. Secondary table (58 words starting at 0xEA3DC4):
 *    - Purpose TBD, cleared during init
 *
 * 3. Lock entries (1792 entries × 28 bytes starting at 0xE935CC):
 *    - Individual lock entry slots organized as a free list
 *    - Entry i has next_free = i+2 (linking to next entry)
 *    - Entry[i].flags cleared
 *
 * 4. Lock control block (at 0xE82128):
 *    - lock_map[]: 251 words cleared
 *    - flag_2cc: Set to 1
 *    - lot_free: Set to 1 (head of free list)
 *    - base_uid: Set to UID_$NIL with low 20 bits replaced by NODE_$ME
 *    - generated_uid: New UID generated via UID_$GEN
 *    - flag_2d0: Cleared
 *
 * 5. UID lock eventcount (at 0xE2C028):
 *    - Initialized via EC_$INIT
 *
 * Finally calls REM_FILE_$UNLOCK_ALL to release any stale remote locks.
 */

#include "file/file_internal.h"
#include "ec/ec.h"
#include "rem_file/rem_file.h"

/*
 * FILE_$LOCK_INIT
 *
 * Assembly at 0x00E32744:
 *   link.w A6,-0x10
 *   movem.l { A5 A3 A2 D2},-(SP)
 *   lea (0xe35154).l,A5
 *   movea.l #0xea202c,A0
 *   moveq #0x39,D0              ; 58 iterations (0x39+1)
 *   ...
 */
void FILE_$LOCK_INIT(void)
{
    int i, j;
    file_lock_entry_t *entry;
    uint16_t *lock_table_ptr;
    uint16_t *table2_ptr;

    /*
     * Initialize lock table (58 entries × 300 bytes)
     * Clear bytes 2-299 of each entry (preserve first 2 bytes)
     *
     * Original loop:
     *   for (i = 0; i < 58; i++) {
     *       for (j = 2; j < 302; j += 2) {
     *           *(uint16_t *)(base + i*300 + j - 0x2662) = 0;
     *       }
     *       *(uint16_t *)(base2 + i*2 + 0x1d98) = 0;
     *   }
     */
    lock_table_ptr = (uint16_t *)((uint8_t *)FILE_$LOCK_TABLE + 2);
    table2_ptr = FILE_$LOCK_TABLE2;

    for (i = 0; i < FILE_LOCK_TABLE_ENTRIES; i++) {
        /* Clear 150 words (bytes 2-299) of this table entry */
        for (j = 0; j < 150; j++) {
            lock_table_ptr[j] = 0;
        }
        /* Clear corresponding word in secondary table */
        *table2_ptr = 0;

        /* Advance to next entry */
        lock_table_ptr = (uint16_t *)((uint8_t *)lock_table_ptr + FILE_LOCK_TABLE_ENTRY_SIZE);
        table2_ptr++;
    }

    /*
     * Initialize lock entries free list (1792 entries × 28 bytes)
     * Set up free list: entry[i].next_free = i+2, entry[i].flags = 0
     *
     * Original loop:
     *   sVar1 = 1;
     *   for (i = 0; i < 1792; i++) {
     *       entry[i].flags = 0;
     *       sVar1++;
     *       entry[i].next_free = sVar1;
     *   }
     * After loop: entry[1791].next_free = 1793 (invalid, marks end)
     */
    entry = FILE_$LOCK_ENTRIES;
    for (i = 0; i < FILE_LOCK_ENTRY_COUNT; i++) {
        entry->flags = 0;
        entry->next_free = (uint16_t)(i + 2);  /* Points to next entry (1-based index) */
        entry++;
    }

    /*
     * Initialize lock control block
     */

    /* Clear lock_map array (251 words) */
    for (i = 0; i < 251; i++) {
        FILE_$LOCK_CONTROL.lock_map[i] = 0;
    }

    /* Initialize UID lock eventcount */
    EC_$INIT(&FILE_$UID_LOCK_EC);

    /* Set lot_free to 1 (first free entry index, 1-based) */
    FILE_$LOCK_CONTROL.lot_free = 1;

    /* Set flag_2cc to 1 */
    FILE_$LOCK_CONTROL.flag_2cc = 1;

    /* Generate a new UID for this initialization */
    UID_$GEN(&FILE_$LOCK_CONTROL.generated_uid);

    /*
     * Set base_uid to UID_$NIL with low 20 bits replaced by NODE_$ME
     * This creates a node-specific base UID for lock identification
     */
    FILE_$LOCK_CONTROL.base_uid.high = UID_$NIL.high;
    FILE_$LOCK_CONTROL.base_uid.low = (UID_$NIL.low & 0xFFF00000) | NODE_$ME;

    /* Clear flag_2d0 */
    FILE_$LOCK_CONTROL.flag_2d0 = 0;

    /* Release any stale remote file locks */
    REM_FILE_$UNLOCK_ALL();
}
