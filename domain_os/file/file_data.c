/*
 * FILE subsystem data definitions
 *
 * Global variables for the file locking subsystem.
 * On m68k, these are at fixed memory addresses. For portability,
 * we define them as regular variables that can be placed by the linker.
 */

#include "file/file_internal.h"

/*
 * Lock control block
 * Original address: 0xE82128
 */
file_lock_control_t FILE_$LOCK_CONTROL;

/*
 * Lock table (58 entries × 300 bytes)
 * Original address: 0xE9F9CC
 */
file_lock_table_entry_t FILE_$LOCK_TABLE[FILE_LOCK_TABLE_ENTRIES];

/*
 * Lock object table.  Original address: 0xE935CC, 0x1C bytes per entry.
 *
 * Element count re-derived from FILE_$LOCK_INIT's free-list loop:
 *   0x00E32784  move.w #0x6ff,D0w        ; dbf trip count -> 0x700 = 1792
 *   0x00E32788  movea.l #0xe935cc,A0
 *   0x00E3278E  moveq #0x1,D1            ; first index is 1, not 0
 *   0x00E32790  lea (0x1c,A0),A0         ; A0 = END of entry 1
 * so entries 1..1792 span 0xE935CC..0xE9B1CB (1792*0x1C = 0x7C00).  Nothing
 * is labelled between 0xE9B1CC and the next known datum 0xE9F9C4, so the
 * table's own loop is the only witness to the count - and it is decisive.
 *
 * The array carries one extra slot (index 1793).  FILE_$LOCK_INIT's last
 * iteration writes 1793 into entry 1792's `next` (0x00E3279E), so 1793 is the
 * value FILE_$LOT_FREE reaches once every entry is allocated; slot 1793 is
 * never initialised and its zero `next` is what terminates the free list at
 * FILE_$PRIV_LOCK_$ALLOC_ENTRY 0x00E5EBB4 (`tst.w (-0x122,A0)` / `beq`).
 * On the m68k image that zero comes from BSS; here it comes from this slot.
 */
file_lock_entry_detail_t FILE_$LOCK_ENTRIES[FILE_LOCK_ENTRY_COUNT + 1];

/*
 * Word at 0xE9F9C4 - cleared by FILE_$LOCK_INIT (0x00E327AC) and referenced
 * nowhere else in the image.
 * The SR10.2 SAU2 link map settles the question as far as it can be settled:
 * sau2.10.2.tar's sau2/domain_os.map is the map for THIS image (it places
 * FILE_$LOCK_INIT at E32744, our address), and it puts 0xE9F9C4 inside the
 * segment `D71  E935CC  FILE_$LOT_DATA  size = 1086C` - E935CC..EA3E38, which
 * ends exactly where the per-ASID count array does.  That segment exports NO
 * symbols at all, so the map cannot name the cell.  Nor can the code: the only
 * table bases the image ever loads in 16-bit displacement range of 0xE9F9C4
 * are #0xE935CC, #0xE97294 and #0xEA202C, and every displacement taken off
 * them in the lock routines is -0x2662 (= 0xE9F9CC).  The word is
 * write-only.  (source-9r49, closed as not-nameable.)
 */
uint16_t FILE_$LOT_E9F9C4;

/*
 * Secondary lock table (58 words)
 * Original address: 0xEA3DC4
 * Purpose TBD - possibly overflow or auxiliary data for lock table
 */
uint16_t FILE_$LOCK_TABLE2[FILE_LOCK_TABLE_ENTRIES];

/*
 * UID lock eventcount
 * Original address: 0xE2C028
 */
ec_$eventcount_t FILE_$UID_LOCK_EC;

/*
 * Per-UID hash-bucket lock holder array (17 bytes)
 *
 * Each byte holds the low byte of the PID of the process that owns
 * the corresponding UID hash bucket lock. Zero means free.
 *
 * On m68k, this array is accessed at (A5 + index), where A5 points
 * to the process data area. For portability, we define it as a
 * regular global array.
 */
uint8_t FILE_$UID_LOCK_HOLDERS[FILE_UID_LOCK_BUCKETS];

/*
 * ============================================================================
 * Lock Compatibility / Mapping Tables (constant data)
 *
 * In the m68k binary these live inside the lock control block at fixed
 * offsets.  For portability they are defined as standalone arrays so the
 * linker places them; the values are copied verbatim from the binary image.
 * ============================================================================
 */

/*
 * Lock conflict matrix (8 entries)
 * Original address: FILE_$LOCK_CONTROL + 0x18 (0xE82140)
 *
 * Indexed by the mapped mode of the *request*; bit M is set when a held lock
 * of mapped mode M is compatible.  Values read straight out of the image.
 */
uint16_t FILE_$LOCK_CONFLICT_TABLE[8] = {
    0x007F, 0x005F, 0x0047, 0x005B, 0x000B, 0x0001, 0x004F, 0x0000
};

/*
 * Lock compatibility table (12 entries)
 * Original address: FILE_$LOCK_CONTROL + 0x28 (0xE82150)
 */
uint16_t FILE_$LOCK_COMPAT_TABLE[12] = {
    0, 4, 6, 2, 6, 4, 2, 4, 2, 0, 1, 2
};

/*
 * Lock map table (12 entries)
 * Original address: FILE_$LOCK_CONTROL + 0x40 (0xE82168)
 */
uint16_t FILE_$LOCK_MAP_TABLE[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 4
};

/*
 * Lock-mode canonicalisation table (12 entries)
 * Original address: FILE_$LOCK_CONTROL + 0x40 (0xE82168)
 *
 * In the m68k binary this shares the same address as LOCK_MAP_TABLE.
 * For portability both symbols are defined; the values are identical.
 */
uint16_t FILE_$LOCK_MODE_MAP[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 4
};

/*
 * Lock mode table (24 entries)
 * Original address: FILE_$LOCK_CONTROL + 0x58 (0xE82180)
 */
uint16_t FILE_$LOCK_MODE_TABLE[24] = {
    0, 3, 4, 5, 5, 3, 4, 4,
    0, 0, 3, 5, 0, 1, 6, 2,
    2, 1, 6, 6, 0, 0, 1, 2
};

/*
 * Lock request table (12 entries)
 * Original address: FILE_$LOCK_CONTROL + 0x88 (0xE821B0)
 */
uint16_t FILE_$LOCK_REQ_TABLE[12] = {
    0, 1, 2, 4, 4, 1, 2, 2, 0, 0, 0x0A, 0x0B
};

/*
 * Lock conversion table (12 entries)
 * Original address: FILE_$LOCK_CONTROL + 0xA0 (0xE821C8)
 */
uint16_t FILE_$LOCK_CVT_TABLE[12] = {
    0, 0x0C16, 0x0C16, 6, 0x0C16, 0x0810, 2, 0x0810,
    0, 0, 0x0C16, 0x0C16
};

/*
 * ============================================================================
 * Runtime state variables (zero-initialized, set by FILE_$LOCK_INIT)
 * ============================================================================
 */

/*
 * Lock hash table (58 entries)
 * Original address: FILE_$LOCK_CONTROL + 0xC8 (0xE821F0)
 */
uint16_t FILE_$LOT_HASHTAB[FILE_LOCK_TABLE_ENTRIES];

/*
 * Lock sequence counter
 * Original address: FILE_$LOCK_CONTROL + 0x2C4 (0xE823EC)
 */
uint32_t FILE_$LOT_SEQN;

/*
 * Default initial file size
 * Original address: FILE_$LOCK_CONTROL + 0x2C0 (0xE823E8)
 */
uint32_t FILE_$DEFAULT_SIZE = 0x1010100F;

/*
 * Lock illegal modes mask
 * Original address: FILE_$LOCK_CONTROL + 0x2C8 (0xE823F0)
 */
uint16_t FILE_$LOCK_ILLEGAL_MASK = 0x00E8;

/*
 * Highest allocated lock entry index
 * Original address: FILE_$LOCK_CONTROL + 0x2CC (0xE823F4)
 */
uint16_t FILE_$LOT_HIGH;

/*
 * Head of free lock entry list
 * Original address: FILE_$LOCK_CONTROL + 0x2CE (0xE823F6)
 */
uint16_t FILE_$LOT_FREE;

/*
 * Count of lock entries whose remote negotiation is outstanding
 * Original address: FILE_$LOCK_CONTROL + 0x2CA (0xE823F2)
 */
uint16_t FILE_$LOT_PENDING;

/*
 * Lock table full flag (Domain boolean: 0xFF = true)
 * Original address: FILE_$LOCK_CONTROL + 0x2D0 (0xE823F8)
 */
int8_t FILE_$LOT_FULL;
