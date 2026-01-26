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
 * File attribute IDs for FILE_$SET_ATTRIBUTE
 */
#define FILE_ATTR_IMMUTABLE         1   /* Immutable flag */
#define FILE_ATTR_TROUBLE           2   /* Trouble flag */
#define FILE_ATTR_TYPE_UID          4   /* Type UID */
#define FILE_ATTR_DIR_PTR           5   /* Directory pointer */
#define FILE_ATTR_DELETE_ON_UNLOCK  7   /* Delete-on-unlock flag */
#define FILE_ATTR_REFCNT            8   /* Reference count */
#define FILE_ATTR_DTM_AST           9   /* DTM (AST compat mode) */
#define FILE_ATTR_DTU_AST           10  /* DTU (AST compat mode) */
#define FILE_ATTR_AUDITED           13  /* Audited flag (0xD) */
#define FILE_ATTR_MGR_ATTR          14  /* Manager attribute */
#define FILE_ATTR_DEVNO             22  /* Device number */
#define FILE_ATTR_DTM_OLD           23  /* DTM (old format) */
#define FILE_ATTR_DTU_FULL          24  /* DTU (full format) */
#define FILE_ATTR_MAND_LOCK         25  /* Mandatory lock flag (0x19) */
#define FILE_ATTR_DTM_CURRENT       26  /* DTM (use current time) */

/*
 * Flag attribute masks for FILE_$SET_ATTRIBUTE
 */
#define FILE_FLAGS_IMMUTABLE_MASK   0x0002FFFF  /* Mask for immutable flag */
#define FILE_FLAGS_TROUBLE_MASK     0x0000FFFF  /* Mask for trouble flag */
#define FILE_FLAGS_AUDITED_MASK     0x0000FFFF  /* Mask for audited flag */
#define FILE_FLAGS_MAND_LOCK_MASK   0x00080000  /* Mask for mandatory lock */

/*
 * Attribute buffer sizes
 */
#define FILE_ATTR_INFO_SIZE         0x7A  /* 122 bytes - compact format */
#define FILE_ATTR_FULL_SIZE         0x90  /* 144 bytes - full format */

/*
 * File status codes (module 0x0F)
 */
#define file_$object_not_found                     0x000F0001  /* Object not found */
#define file_$object_is_remote                     0x000F0002  /* Object is remote */
#define file_$bad_reply_received_from_remote_node  0x000F0003  /* Bad reply from remote */
#define file_$object_not_locked_by_this_process    0x000F0005  /* Not locked by this process */
#define file_$object_in_use                        0x000F0006  /* Object in use */
#define file_$illegal_lock_request                 0x000F0008  /* Illegal lock request */
#define file_$local_lock_table_full                0x000F0009  /* Lock table full */
#define file_$cannot_create_on_remote_with_uid     0x000F000B  /* Cannot create on remote with UID */
#define file_$obj_not_locked_by_this_process       0x000F000C  /* Not locked by this process (alt) */
#define file_$objects_on_different_volumes         0x000F0013  /* Objects on different volumes */
#define file_$invalid_arg                          0x000F0014  /* Invalid argument */
#define file_$incompatible_request                 0x000F0015  /* Incompatible request */
#define file_$invalid_type                         0x000F0016  /* Invalid type */
#define file_$op_cannot_perform_here               0x000F0018  /* Cannot perform operation here */
#define file_$vol_mounted_read_only                0x000E0030  /* Volume is read-only */
#define status_$naming_vol_mounted_read_only       0x00040014  /* Volume mounted read-only (naming) */
#define status_$insufficient_rights                0x000F0011  /* Insufficient rights */

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
 * Core function for setting file attributes. Handles both local and remote
 * files, checking ACL permissions as needed.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   attr_id    - Attribute ID to set (see FILE_ATTR_* constants)
 *   value      - Pointer to attribute value
 *   flags      - Operation flags (low 16 bits = required rights, high 16 bits = option flags)
 *   status_ret - Receives operation status
 *
 * Attribute IDs:
 *   4  = Type UID
 *   5  = Directory pointer
 *   7  = Delete-on-unlock flag
 *   8  = Reference count
 *   9  = DTM (AST compat)
 *   10 = DTU (AST compat)
 *   14 = Manager attribute
 *   22 = Device number
 *   23 = DTM (old format)
 *   24 = DTU (full format)
 *   26 = DTM (use current time)
 *
 * Original address: 0x00E5D242
 */
void FILE_$SET_ATTRIBUTE(uid_t *file_uid, int16_t attr_id, void *value,
                         uint32_t flags, status_$t *status_ret);

/*
 * FILE_$GET_ATTR_INFO - Get file attribute info (compact format)
 *
 * Returns file attributes in a compact 122-byte format (0x7A).
 * Checks lock status if requested via param_2 flags.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   param_2    - Pointer to flags byte (bit 0=check lock, bit 1=check delete, bit 2=skip delete check)
 *   size_ptr   - Pointer to buffer size (must be 0x7A = 122)
 *   uid_out    - Output UID buffer (32 bytes = 8 longs)
 *   attr_out   - Output attribute buffer (structure TBD)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5D7F4
 */
void FILE_$GET_ATTR_INFO(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                         uint32_t *uid_out, void *attr_out, status_$t *status_ret);

/*
 * FILE_$GET_ATTRIBUTES - Get file attributes (full format)
 *
 * Returns file attributes in full 144-byte format (0x90).
 *
 * Parameters:
 *   file_uid   - UID of file
 *   param_2    - Pointer to flags
 *   size_ptr   - Pointer to buffer size (must be 0x90 = 144)
 *   uid_out    - Output UID buffer (32 bytes)
 *   attr_out   - Output attribute buffer (144 bytes)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5D984
 */
void FILE_$GET_ATTRIBUTES(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                          uint32_t *uid_out, void *attr_out, status_$t *status_ret);

/*
 * FILE_$ATTRIBUTES - Get file attributes (old format)
 *
 * Returns file attributes converted to old format via VTOCE_$NEW_TO_OLD.
 * Uses attribute flags 0x21.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   attr_out   - Output buffer for old-format attributes
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DA4C
 */
void FILE_$ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret);

/*
 * FILE_$ACT_ATTRIBUTES - Get active file attributes (old format)
 *
 * Like FILE_$ATTRIBUTES but uses flags 0x01 (locked access).
 *
 * Parameters:
 *   file_uid   - UID of file
 *   attr_out   - Output buffer for old-format attributes
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DAB0
 */
void FILE_$ACT_ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret);

/*
 * FILE_$SET_TYPE - Set file type UID
 *
 * Parameters:
 *   file_uid - UID of file to modify
 *   type_uid - New type UID
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E176
 */
void FILE_$SET_TYPE(uid_t *file_uid, uid_t *type_uid, status_$t *status_ret);

/*
 * FILE_$SET_DIRPTR - Set file directory pointer
 *
 * Parameters:
 *   file_uid - UID of file to modify
 *   dir_uid  - UID of parent directory
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E1B2
 */
void FILE_$SET_DIRPTR(uid_t *file_uid, uid_t *dir_uid, status_$t *status_ret);

/*
 * FILE_$SET_DTM_F - Set Data Time Modified (full version)
 *
 * Sets the DTM attribute. If flag param is negative, uses current time.
 * Falls back to AST_$SET_ATTRIBUTE if incompatible request.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   flags      - Pointer to flags byte (negative = use current time)
 *   time_value - Pointer to time value (uint32_t + uint16_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E1EE
 */
void FILE_$SET_DTM_F(uid_t *file_uid, int8_t *flags, void *time_value,
                     status_$t *status_ret);

/*
 * FILE_$SET_DTM - Set Data Time Modified (simple version)
 *
 * Wrapper for FILE_$SET_DTM_F with explicit time value.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E28E
 */
void FILE_$SET_DTM(uid_t *file_uid, uint32_t *time_value, status_$t *status_ret);

/*
 * FILE_$SET_DTU_F - Set Data Time Used (full version)
 *
 * Sets the DTU (access time) attribute.
 * Falls back to AST_$SET_ATTRIBUTE if incompatible request.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t + uint16_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E2C2
 */
void FILE_$SET_DTU_F(uid_t *file_uid, void *time_value, status_$t *status_ret);

/*
 * FILE_$SET_DTU - Set Data Time Used (simple version)
 *
 * Wrapper for FILE_$SET_DTU_F.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E332
 */
void FILE_$SET_DTU(uid_t *file_uid, uint32_t *time_value, status_$t *status_ret);

/*
 * FILE_$SET_DEVNO - Set device number
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   devno      - Pointer to device number (uint16_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E362
 */
void FILE_$SET_DEVNO(uid_t *file_uid, uint16_t *devno, status_$t *status_ret);

/*
 * FILE_$SET_MGR_ATTR - Set manager attribute
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   mgr_attr   - Pointer to manager attribute (8 bytes)
 *   version    - Pointer to version (must be 0)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E39A
 */
void FILE_$SET_MGR_ATTR(uid_t *file_uid, void *mgr_attr, int16_t *version,
                        status_$t *status_ret);

/*
 * FILE_$SET_REFCNT - Set reference count
 *
 * If reference count is 0xFFFFFFFF, sets to 0.
 * If >= 0xFFF5, sets to -11.
 * If refcnt becomes 0 and status is OK, deletes the file.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   refcnt     - Pointer to new reference count
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E3F4
 */
void FILE_$SET_REFCNT(uid_t *file_uid, uint32_t *refcnt, status_$t *status_ret);

/*
 * ============================================================================
 * File Flag Functions
 * ============================================================================
 */

/*
 * FILE_$MK_PERMANENT - Mark file as permanent
 *
 * Stub function that always returns success. The permanent flag is
 * likely handled elsewhere in the system.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   status_ret - Receives operation status (always status_$ok)
 *
 * Original address: 0x00E5DBA4
 */
void FILE_$MK_PERMANENT(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$MK_TEMPORARY - Mark file as temporary
 *
 * Stub function that always returns success. The temporary flag is
 * likely handled elsewhere in the system.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   status_ret - Receives operation status (always status_$ok)
 *
 * Original address: 0x00E5DBB2
 */
void FILE_$MK_TEMPORARY(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$MK_IMMUTABLE - Mark file as immutable
 *
 * Sets the immutable flag on a file, preventing modifications.
 * Uses FILE_$SET_ATTRIBUTE with attr_id=1.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DBC0
 */
void FILE_$MK_IMMUTABLE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$SET_AUDITED - Set file audit flags
 *
 * Sets the audit flags for a file. Requires audit administrator privileges.
 * The two boolean parameters control different aspects of auditing:
 *   - param_2 < 0 sets bit 0 of the audit flags
 *   - param_3 < 0 sets bit 1 of the audit flags
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   param_2    - Pointer to first audit flag (negative = enable)
 *   param_3    - Pointer to second audit flag (negative = enable)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DBE8
 */
void FILE_$SET_AUDITED(uid_t *file_uid, int8_t *param_2, int8_t *param_3,
                       status_$t *status_ret);

/*
 * FILE_$SET_TROUBLE - Set file trouble flag
 *
 * Sets the trouble flag on a file, indicating some issue with the file.
 * Uses FILE_$SET_ATTRIBUTE with attr_id=2.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   unused     - Unused parameter (preserved for API compatibility)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DC42
 */
void FILE_$SET_TROUBLE(uid_t *file_uid, void *unused, status_$t *status_ret);

/*
 * FILE_$SET_MAND_LOCK - Set mandatory lock flag
 *
 * Sets or clears the mandatory lock flag on a file.
 * Uses FILE_$SET_ATTRIBUTE with attr_id=25 (0x19).
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   flag       - Pointer to lock flag value
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DC6A
 */
void FILE_$SET_MAND_LOCK(uid_t *file_uid, uint8_t *flag, status_$t *status_ret);

/*
 * ============================================================================
 * File Locking Functions
 * ============================================================================
 */

/*
 * Lock mode flags for FILE_$PRIV_LOCK param_6
 * These flags control locking behavior:
 */
#define FILE_LOCK_FLAG_REMOTE       0x020000   /* Lock is from remote request */
#define FILE_LOCK_FLAG_LOCAL_ONLY   0x010000   /* Skip remote flush */
#define FILE_LOCK_FLAG_CHANGE       0x400000   /* Change existing lock */
#define FILE_LOCK_FLAG_UPGRADE      0x040000   /* Upgrade lock mode */
#define FILE_LOCK_FLAG_NO_RIGHTS    0x080000   /* Skip rights check */

/*
 * FILE_$LOCK_D - Lock a file with domain context
 *
 * Locks a file with explicit domain (distributed) context.
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   lock_index   - Pointer to lock index (from previous lock or 0)
 *   lock_mode    - Pointer to lock mode
 *   rights       - Pointer to rights byte
 *   param_5      - Additional parameter
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5EA2A
 */
void FILE_$LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                  uint8_t *rights, uint32_t param_5, status_$t *status_ret);

/*
 * FILE_$CHANGE_LOCK_D - Change an existing lock with domain context
 *
 * Changes the mode of an existing lock.
 *
 * Parameters:
 *   file_uid     - UID of file with existing lock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to new lock mode
 *   param_4      - Additional parameter
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5EA9E
 */
void FILE_$CHANGE_LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                         uint32_t param_4, status_$t *status_ret);

/*
 * FILE_$LOCK - Lock a file
 *
 * Standard file locking function.
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   lock_index   - Pointer to lock index (uint16_t)
 *   lock_mode    - Pointer to lock mode (uint16_t)
 *   rights       - Pointer to rights byte (uint8_t)
 *   lock_info    - Output buffer for lock info (8 bytes)
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5EB20
 */
void FILE_$LOCK(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                uint8_t *rights, void *lock_info, status_$t *status_ret);

/*
 * FILE_$UNLOCK_D - Unlock a file with domain context
 *
 * Unlocks a file with explicit domain (distributed) context.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to lock mode
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5FCC2
 */
void FILE_$UNLOCK_D(uid_t *file_uid, uint32_t *lock_index, uint16_t *lock_mode,
                    status_$t *status_ret);

/*
 * FILE_$UNLOCK - Unlock a file
 *
 * Standard file unlocking function.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_mode    - Pointer to lock mode
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5FCFC
 */
void FILE_$UNLOCK(uid_t *file_uid, uint16_t *lock_mode, status_$t *status_ret);

/*
 * FILE_$UNLOCK_VOL - Unlock all locks on a volume
 *
 * Releases all locks held on a specific volume.
 *
 * Parameters:
 *   vol_uid      - UID of volume
 *   status_ret   - Output status code
 *
 * Original address: 0x00E60D36
 */
void FILE_$UNLOCK_VOL(uid_t *vol_uid, status_$t *status_ret);

/*
 * FILE_$FORCE_UNLOCK - Force unlock a file
 *
 * Forces release of a lock, even if not held by current process.
 * Only works for locks on the local node.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   status_ret   - Output status code
 *
 * Original address: 0x00E60DB0
 */
void FILE_$FORCE_UNLOCK(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$UNLOCK_PROC - Unlock all locks for a process
 *
 * Releases all locks held by a specific process.
 *
 * Parameters:
 *   proc_uid     - Process UID (or UID_$NIL for current process)
 *   file_uid     - File UID to unlock (or UID_$NIL for all)
 *   lock_mode    - Pointer to lock mode filter
 *   param_4      - Additional parameter
 *   status_ret   - Output status code
 *
 * Original address: 0x00E60E3E
 */
void FILE_$UNLOCK_PROC(uid_t *proc_uid, uid_t *file_uid, uint16_t *lock_mode,
                       uint32_t param_4, status_$t *status_ret);

/*
 * ============================================================================
 * Lock Query Functions
 * ============================================================================
 */

/*
 * FILE_$LOCATE - Get file location from UID
 *
 * Retrieves the location (network node) information for a file object.
 *
 * Parameters:
 *   file_uid     - Pointer to file UID to locate
 *   location_out - Output: receives location info (uint32_t node address)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E60620
 */
void FILE_$LOCATE(uid_t *file_uid, uint32_t *location_out, status_$t *status_ret);

/*
 * FILE_$LOCATEI - Get file location with diskless fallback
 *
 * Extended version of FILE_$LOCATE that handles diskless client UIDs.
 * If normal location lookup fails and UID appears to be a diskless UID,
 * computes location from the UID structure.
 *
 * Parameters:
 *   file_uid     - Pointer to file UID to locate
 *   location_out - Output: receives location UID (high + low)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E6067C
 */
void FILE_$LOCATEI(uid_t *file_uid, uid_t *location_out, status_$t *status_ret);

/*
 * FILE_$READ_LOCK_ENTRY - Read lock entry information
 *
 * Reads information about a lock on a file. Iterates through locks.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   index      - Pointer to iteration index (starts at 1, updated on return)
 *   info_out   - Output buffer for lock info (26 bytes minimum)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E608EC
 */
void FILE_$READ_LOCK_ENTRY(uid_t *file_uid, uint16_t *index,
                            void *info_out, status_$t *status_ret);

/*
 * FILE_$READ_LOCK_ENTRYU - Read lock entry by UID
 *
 * Reads lock entry information for a file by its UID.
 * Handles both local and remote files.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   info_out   - Output buffer for lock info (26 bytes minimum)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E6042E
 */
void FILE_$READ_LOCK_ENTRYU(uid_t *file_uid, void *info_out, status_$t *status_ret);

/*
 * FILE_$IMPORT_LK - Import a lock from another process
 *
 * Validates a lock index and returns the validated index if the lock
 * exists and matches the specified file UID.
 *
 * Parameters:
 *   file_uid   - UID of file the lock should be on
 *   index_in   - Pointer to lock index to validate
 *   index_out  - Output: validated lock index
 *   status_ret - Output: status code
 *
 * Original address: 0x00E603AC
 */
void FILE_$IMPORT_LK(uid_t *file_uid, uint32_t *index_in, uint32_t *index_out,
                      status_$t *status_ret);

/*
 * ============================================================================
 * File Protection/ACL Functions
 * ============================================================================
 */

/*
 * FILE_$CHECK_PROT - Check file protection/access rights
 *
 * Checks if the current process has the requested access rights to a file.
 * First checks a per-process lock cache, then falls back to ACL_$RIGHTS.
 *
 * Parameters:
 *   file_uid    - UID of file to check
 *   access_mask - Required access rights mask
 *   slot_num    - Lock table slot number (0-149)
 *   unused      - Unused parameter
 *   rights_out  - Output: actual rights available
 *   status_ret  - Output: status code
 *
 * Returns:
 *   1 if rights check completed (check status for success/failure)
 *   Result from ACL_$RIGHTS otherwise
 *
 * Original address: 0x00E5D172
 */
int16_t FILE_$CHECK_PROT(uid_t *file_uid, uint16_t access_mask, uint32_t slot_num,
                         void *unused, uint16_t *rights_out, status_$t *status_ret);

/*
 * FILE_$SET_PROT - Set file protection
 *
 * Sets file protection based on protection type. Maps protection types
 * to attribute IDs and handles both local and remote files.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   prot_type  - Pointer to protection type (0-6)
 *   acl_data   - ACL data buffer (44 bytes)
 *   acl_uid    - ACL UID (8 bytes) - flags encoded in low word
 *   status_ret - Output status code
 *
 * Protection types:
 *   0-5: Standard protection modes (mapped to attr IDs 0x10-0x15)
 *   6: Set protection by ACL UID
 *
 * The acl_uid parameter encodes special flags in the low word:
 *   Bits 4-11 (masked with 0xFF0 >> 4): If bit 4 is set, use default protection
 *
 * Original address: 0x00E5DF3A
 */
void FILE_$SET_PROT(uid_t *file_uid, uint16_t *prot_type, uint32_t *acl_data,
                    uid_t *acl_uid, status_$t *status_ret);

/*
 * FILE_$SET_ACL - Set file ACL
 *
 * Sets the access control list for a file using a "funky" ACL format.
 * Converts the ACL format and calls FILE_$SET_PROT.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   acl_uid    - ACL UID in funky format (type encoded in low word)
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E06A
 */
void FILE_$SET_ACL(uid_t *file_uid, uid_t *acl_uid, status_$t *status_ret);

/*
 * FILE_$OLD_AP - Set file access protection (old/legacy interface)
 *
 * Legacy function for setting file access protection.
 * Used for backward compatibility with older protection schemes.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   prot_type  - Pointer to protection type
 *   acl_data   - ACL data buffer (44 bytes)
 *   acl_uid    - ACL UID (8 bytes)
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E100
 */
void FILE_$OLD_AP(uid_t *file_uid, int16_t *prot_type, uint32_t *acl_data,
                  uint32_t *acl_uid, status_$t *status_ret);

/*
 * ============================================================================
 * File Write Functions
 * ============================================================================
 */

/*
 * FILE_$FW_FILE - Force Write File
 *
 * Forces all dirty pages of a file to be written back to disk.
 * This ensures file data durability by flushing the kernel buffer cache.
 *
 * Operation:
 *   1. Checks if file is locked (via FILE_$DELETE_INT query)
 *   2. Calls AST_$PURIFY with appropriate flags:
 *      - If locked: flags = 0x0002 (local only)
 *      - If not locked: flags = 0x8002 (include remote sync)
 *
 * Parameters:
 *   file_uid   - UID of file to flush
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E622
 */
void FILE_$FW_FILE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$FW_PARTIAL - Force Write Partial File
 *
 * Forces dirty pages within a byte range to be written back to disk.
 * Iterates through pages covered by the byte range and purifies each.
 *
 * Page size is 32KB (0x8000 bytes). The function calculates the starting
 * page from the offset and walks through subsequent pages until the
 * byte count is exhausted.
 *
 * Parameters:
 *   file_uid     - UID of file to flush
 *   start_offset - Pointer to starting byte offset
 *   byte_count   - Pointer to number of bytes to flush
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5E680
 */
void FILE_$FW_PARTIAL(uid_t *file_uid, uint32_t *start_offset,
                      int32_t *byte_count, status_$t *status_ret);

/*
 * FILE_$FW_PAGES - Force Write Specific Pages
 *
 * Forces specific pages to be written back to disk. Takes a list of
 * page entries where each entry contains:
 *   bits 5-31: page number
 *   bits 0-4:  sub-page index
 *
 * Pages are processed in batches of up to 32, with each batch sorted
 * for sequential I/O optimization before writing.
 *
 * Parameters:
 *   file_uid   - UID of file to flush
 *   page_list  - Array of page entry values
 *   page_count - Pointer to number of pages in list
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E71E
 */
void FILE_$FW_PAGES(uid_t *file_uid, uint32_t *page_list, uint16_t *page_count,
                    status_$t *status_ret);

/*
 * ============================================================================
 * File Miscellaneous Functions
 * ============================================================================
 */

/*
 * FILE_$NEIGHBORS - Check if two files are on the same volume
 *
 * Checks if two files are "neighbors" (located on the same volume).
 * Wrapper around FILE_$CHECK_SAME_VOLUME.
 *
 * Parameters:
 *   file_uid1  - First file UID
 *   file_uid2  - Second file UID
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E5AE
 */
void FILE_$NEIGHBORS(uid_t *file_uid1, uid_t *file_uid2, status_$t *status_ret);

/*
 * FILE_$PURIFY - Purify (flush) a file's dirty pages
 *
 * Flushes dirty pages of a file to disk by calling AST_$PURIFY.
 * This performs a basic purification with default flags.
 *
 * Parameters:
 *   file_uid   - UID of file to purify
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E5F2
 */
void FILE_$PURIFY(uid_t *file_uid, status_$t *status_ret);

/*
 * ============================================================================
 * File Truncation/Length Functions
 * ============================================================================
 */

/*
 * FILE_$SET_LEN - Set file length
 *
 * Sets the length of a file. Equivalent to truncation if shrinking.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   new_length - Pointer to new length value
 *   status_ret - Output status code
 *
 * Original address: 0x00E73F90
 */
void FILE_$SET_LEN(uid_t *file_uid, uint32_t *new_length, status_$t *status_ret);

/*
 * FILE_$SET_LEN_D - Set file length with domain context
 *
 * Sets the length of a file with explicit domain context for
 * distributed locking.
 *
 * Parameters:
 *   file_uid    - UID of file to modify
 *   new_length  - Pointer to new length value
 *   domain_ctx  - Pointer to domain context (lock index)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E73FB0
 */
void FILE_$SET_LEN_D(uid_t *file_uid, uint32_t *new_length,
                     uint32_t *domain_ctx, status_$t *status_ret);

/*
 * FILE_$TRUNCATE - Truncate a file
 *
 * Truncates a file to the specified size.
 *
 * Parameters:
 *   file_uid   - UID of file to truncate
 *   new_size   - Pointer to new size value
 *   status_ret - Output status code
 *
 * Original address: 0x00E73FCA
 */
void FILE_$TRUNCATE(uid_t *file_uid, uint32_t *new_size, status_$t *status_ret);

/*
 * FILE_$TRUNCATE_D - Truncate a file with domain context
 *
 * Truncates a file to the specified size with explicit domain context.
 * This is the core implementation used by FILE_$TRUNCATE and FILE_$SET_LEN.
 *
 * Parameters:
 *   file_uid    - UID of file to truncate
 *   new_size    - Pointer to new size value
 *   domain_ctx  - Pointer to domain context (lock index)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E73FE4
 */
void FILE_$TRUNCATE_D(uid_t *file_uid, uint32_t *new_size,
                      uint32_t *domain_ctx, status_$t *status_ret);

/*
 * ============================================================================
 * File Space Management Functions
 * ============================================================================
 */

/*
 * FILE_$RESERVE - Reserve disk space for a file
 *
 * Pre-allocates disk space for a file to ensure contiguous space
 * and prevent fragmentation.
 *
 * Parameters:
 *   file_uid   - UID of file to reserve space for
 *   start_byte - Pointer to starting byte offset
 *   byte_count - Pointer to number of bytes to reserve
 *   status_ret - Output status code
 *
 * Original address: 0x00E74310
 */
void FILE_$RESERVE(uid_t *file_uid, uint32_t *start_byte,
                   uint32_t *byte_count, status_$t *status_ret);

/*
 * FILE_$INVALIDATE - Invalidate cached pages of a file
 *
 * Forces cached pages for a file to be discarded, ensuring that
 * subsequent reads will fetch fresh data from disk.
 *
 * Parameters:
 *   file_uid    - UID of file to invalidate
 *   start_page  - Pointer to starting page number
 *   page_count  - Pointer to number of pages to invalidate
 *   flags       - Pointer to flags byte
 *   status_ret  - Output status code
 *
 * Original address: 0x00E75158
 */
void FILE_$INVALIDATE(uid_t *file_uid, uint32_t *start_page,
                      uint32_t *page_count, uint8_t *flags,
                      status_$t *status_ret);

/*
 * FILE_$GET_SEG_MAP - Get segment map for a file
 *
 * Retrieves a bitmap showing which segments of a file are currently
 * allocated or mapped.
 *
 * Parameters:
 *   file_uid    - UID of file to query
 *   start_off   - Pointer to starting offset for segment query
 *   flags_in    - Pointer to flags (negative = set flag bit 0)
 *   bitmap_out  - Output bitmap (32 bits)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E74044
 */
void FILE_$GET_SEG_MAP(uid_t *file_uid, uint32_t *start_off,
                       int8_t *flags_in, uint32_t *bitmap_out,
                       status_$t *status_ret);

/*
 * ============================================================================
 * Lock Export Functions
 * ============================================================================
 */

/*
 * FILE_$EXPORT_LK - Export a lock to another process
 *
 * Exports a file lock held by the current process to another process,
 * allowing the target process to share the lock.
 *
 * Parameters:
 *   file_uid    - UID of file that must match the locked file
 *   lock_index  - Pointer to lock index to export
 *   target_proc - UID of target process to export lock to
 *   index_out   - Output: lock index assigned in target's table
 *   status_ret  - Output status code
 *
 * Original address: 0x00E74110
 */
void FILE_$EXPORT_LK(uid_t *file_uid, uint32_t *lock_index,
                     uid_t *target_proc, int32_t *index_out,
                     status_$t *status_ret);

/*
 * FILE_$UNLOCK_ALL - Unlock all locks for current process
 *
 * Releases all file locks held by the current process.
 * Typically called during process cleanup.
 *
 * Original address: 0x00E75138
 */
void FILE_$UNLOCK_ALL(void);

#endif /* FILE_H */
