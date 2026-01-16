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
