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
 * Lock entries (1792 entries × 28 bytes)
 * Original address: 0xE935CC
 */
file_lock_entry_t FILE_$LOCK_ENTRIES[FILE_LOCK_ENTRY_COUNT];

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
 * ============================================================================
 * Lock Compatibility / Mapping Tables (constant data)
 *
 * In the m68k binary these live inside the lock control block at fixed
 * offsets.  For portability they are defined as standalone arrays so the
 * linker places them; the values are copied verbatim from the binary image.
 * ============================================================================
 */

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
 * ASID map table (12 entries)
 * Original address: FILE_$LOCK_CONTROL + 0x40 (0xE82168)
 *
 * In the m68k binary this shares the same address as LOCK_MAP_TABLE.
 * For portability both symbols are defined; the values are identical.
 */
uint16_t FILE_$ASID_MAP[12] = {
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
 * Lock table full flag
 * Original address: FILE_$LOCK_CONTROL + 0x2D0 (0xE823F8)
 */
uint8_t FILE_$LOT_FULL;
