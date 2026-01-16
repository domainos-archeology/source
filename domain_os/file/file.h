/*
 * FILE - File Operations Module
 *
 * This module provides file operations for Domain/OS including:
 * - File locking (advisory and mandatory)
 * - File attributes (create, delete, set/get attributes)
 * - File protection and ACLs
 * - Remote file operations (via REM_FILE_$)
 *
 * Memory layout (m68k):
 *   - Lock control block: 0xE82128
 *   - Lock table: 0xE9F9CC (58 entries × 300 bytes)
 *   - Lock entries: 0xE935CC (1792 entries × 28 bytes)
 *   - UID lock eventcount: 0xE2C028
 */

#ifndef FILE_H
#define FILE_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Number of entries in the lock table (hash buckets) */
#define FILE_LOCK_TABLE_ENTRIES     58

/* Number of lock entry slots */
#define FILE_LOCK_ENTRY_COUNT       1792

/* Size of each lock table entry in bytes */
#define FILE_LOCK_TABLE_ENTRY_SIZE  300

/* Size of each lock entry in bytes */
#define FILE_LOCK_ENTRY_SIZE        28

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * File lock entry structure
 * Size: 28 bytes (0x1C)
 *
 * Used for tracking individual file locks. Organized as a free list
 * during initialization, then allocated as locks are requested.
 */
typedef struct file_lock_entry_t {
    uint8_t     data[0x14];     /* 0x00: Lock-specific data (TBD) */
    uint16_t    next_free;      /* 0x14: Next free entry index (free list) */
    uint16_t    reserved1;      /* 0x16: Reserved */
    uint8_t     flags;          /* 0x18: Lock flags */
    uint8_t     reserved2[3];   /* 0x19: Padding to 28 bytes */
} file_lock_entry_t;

/*
 * File lock table entry structure
 * Size: 300 bytes (0x12C)
 *
 * Hash bucket entry for file lock lookups by UID.
 * First 2 bytes are preserved during init (possibly count or head pointer).
 */
typedef struct file_lock_table_entry_t {
    uint16_t    header;         /* 0x00: Entry header (preserved during init) */
    uint8_t     data[298];      /* 0x02: Lock chain data (cleared during init) */
} file_lock_table_entry_t;

/*
 * File lock control block structure
 * Located at 0xE82128 on m68k
 * Size: at least 721 bytes (0x2D1)
 */
typedef struct file_lock_control_t {
    uint8_t     reserved1[0xb8];    /* 0x00: Reserved/unknown */
    uid_t       base_uid;           /* 0xB8: Base UID (derived from UID_$NIL + NODE_$ME) */
    uid_t       generated_uid;      /* 0xC0: UID generated at init */
    uint16_t    lock_map[251];      /* 0xC8: Lock mapping table (cleared at init) */
    uint16_t    flag_2cc;           /* 0x2CC: Flag (set to 1 at init) */
    uint16_t    lot_free;           /* 0x2CE: FILE_$LOT_FREE - head of free list */
    uint8_t     flag_2d0;           /* 0x2D0: Flag (cleared at init) */
} file_lock_control_t;

/*
 * ============================================================================
 * External Data References
 * ============================================================================
 */

/* Lock control block */
extern file_lock_control_t FILE_$LOCK_CONTROL;

/* Lock table (58 entries) */
extern file_lock_table_entry_t FILE_$LOCK_TABLE[];

/* Lock entries (1792 entries) */
extern file_lock_entry_t FILE_$LOCK_ENTRIES[];

/* UID lock eventcount */
extern ec_$eventcount_t FILE_$UID_LOCK_EC;

/* Free list head (alias into control block) */
extern uint16_t FILE_$LOT_FREE;

/*
 * ============================================================================
 * Initialization Functions
 * ============================================================================
 */

/*
 * FILE_$LOCK_INIT - Initialize file locking subsystem
 *
 * Initializes:
 *   - Lock table (58 entries × 300 bytes)
 *   - Lock entries free list (1792 entries × 28 bytes)
 *   - Lock control block
 *   - UID lock eventcount
 *   - Calls REM_FILE_$UNLOCK_ALL to release any stale remote locks
 *
 * Original address: 0x00E32744
 */
void FILE_$LOCK_INIT(void);

/*
 * ============================================================================
 * File Creation/Deletion Functions
 * ============================================================================
 */

/*
 * FILE_$PRIV_CREATE - Create a file (internal/privileged)
 *
 * Core file creation function used by all public FILE_$CREATE variants.
 *
 * Parameters:
 *   file_type    - Type of file to create (0=default, 1=dir, 2=link, etc.)
 *   type_uid     - UID for typed objects (or UID_$NIL)
 *   dir_uid      - Directory to create file in
 *   file_uid_ret - Receives the new file's UID
 *   initial_size - Initial file size (or 0 for default)
 *   flags        - Creation flags
 *   owner_info   - Owner/ACL info (or NULL)
 *   status_ret   - Receives operation status
 *
 * Original address: 0x00E5D382
 */
uint32_t FILE_$PRIV_CREATE(int16_t file_type, const uid_t *type_uid, uid_t *dir_uid,
                           uid_t *file_uid_ret, uint32_t initial_size,
                           uint16_t flags, uid_t *owner_info, status_$t *status_ret);

/*
 * FILE_$CREATE - Create a new file
 *
 * Creates a default file in the specified directory.
 *
 * Parameters:
 *   dir_uid      - Directory to create file in
 *   file_uid_ret - Receives the new file's UID
 *   status_ret   - Receives operation status
 *
 * Original address: 0x00E5D778
 */
void FILE_$CREATE(uid_t *dir_uid, uid_t *file_uid_ret, status_$t *status_ret);

/*
 * FILE_$CREATE_IT - Create a typed file
 *
 * Creates a file with a specified type.
 *
 * Parameters:
 *   type_ptr     - Pointer to file type value (0, 4, or 5 valid)
 *   type_uid     - UID for the type
 *   dir_uid      - Directory to create file in
 *   size_ptr     - Pointer to initial size
 *   file_uid_ret - Receives the new file's UID
 *   status_ret   - Receives operation status
 *
 * Returns:
 *   Byte indicating success
 *
 * Original address: 0x00E5D79E
 */
uint8_t FILE_$CREATE_IT(int16_t *type_ptr, uid_t *type_uid, uid_t *dir_uid,
                        uint32_t *size_ptr, uid_t *file_uid_ret, status_$t *status_ret);

/*
 * FILE_$DELETE_INT - Delete a file (internal)
 *
 * Internal delete handler used by all public delete functions.
 *
 * Parameters:
 *   file_uid   - UID of file to delete
 *   flags      - Delete flags (bit 0=do delete, bit 1=force, bit 2=set delete-on-unlock)
 *   result     - Receives result byte
 *   status_ret - Receives operation status
 *
 * Returns:
 *   -1 if file was locked, 0 otherwise
 *
 * Original address: 0x00E5E8E0
 */
int8_t FILE_$DELETE_INT(uid_t *file_uid, uint16_t flags, uint8_t *result, status_$t *status_ret);

/*
 * FILE_$DELETE - Delete a file
 *
 * Original address: 0x00E5DB50
 */
void FILE_$DELETE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$DELETE_OBJ - Delete a file object
 *
 * Parameters:
 *   file_uid   - UID of file to delete
 *   force      - If negative, use "force" mode with delete-on-unlock
 *   param_3    - Additional parameter
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DB12
 */
void FILE_$DELETE_OBJ(uid_t *file_uid, int8_t force, void *param_3, status_$t *status_ret);

/*
 * FILE_$DELETE_FORCE - Force delete a file
 *
 * Original address: 0x00E5DB7A
 */
void FILE_$DELETE_FORCE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$DELETE_WHEN_UNLOCKED - Delete a file when unlocked
 *
 * Original address: 0x00E5FC3A
 */
void FILE_$DELETE_WHEN_UNLOCKED(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$DELETE_FORCE_WHEN_UNLOCKED - Force delete when unlocked
 *
 * Original address: 0x00E5FC64
 */
void FILE_$DELETE_FORCE_WHEN_UNLOCKED(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$REMOVE_WHEN_UNLOCKED - Remove a file when unlocked
 *
 * Parameters:
 *   file_uid   - UID of file to remove
 *   result     - Receives result byte
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5FC8E
 */
void FILE_$REMOVE_WHEN_UNLOCKED(uid_t *file_uid, uint8_t *result, status_$t *status_ret);

/*
 * ============================================================================
 * File Attribute Functions
 * ============================================================================
 */

/*
 * FILE_$SET_ATTRIBUTE - Set a file attribute
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   attr_id    - Attribute ID to set (7 = delete-on-unlock, etc.)
 *   value      - Pointer to attribute value
 *   flags      - Operation flags
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5D242
 */
void FILE_$SET_ATTRIBUTE(uid_t *file_uid, uint16_t attr_id, void *value,
                         uint16_t flags, status_$t *status_ret);

/*
 * ============================================================================
 * File Locking Functions
 * ============================================================================
 */

/*
 * FILE_$LOCK - Lock a file
 * Original address: 0x00E5EB20
 */
void FILE_$LOCK(void);

/*
 * FILE_$UNLOCK - Unlock a file
 * Original address: 0x00E5FCFC
 */
void FILE_$UNLOCK(void);

#endif /* FILE_H */
