/*
 * FILE - File Operations Module (Internal Header)
 *
 * Internal types, constants, and helper functions used only within
 * the FILE subsystem.
 */

#ifndef FILE_INTERNAL_H
#define FILE_INTERNAL_H

#include "file/file.h"
#include "uid/uid.h"
#include "ast/ast.h"
#include "acl/acl.h"
#include "time/time.h"
#include "hint/hint.h"
#include "rem_file/rem_file.h"
#include "audit/audit.h"
#include "network/network.h"
#include "route/route.h"
#include "disk/disk.h"
#include "proc1/proc1.h"
#include "vtoc/vtoc.h"

/*
 * ============================================================================
 * Memory Addresses (m68k)
 * ============================================================================
 */

#ifdef M68K_TARGET
/* Lock control block base address */
#define FILE_LOCK_CONTROL_ADDR      0xE82128

/* Lock table base address (58 entries × 300 bytes) */
#define FILE_LOCK_TABLE_ADDR        0xE9F9CC

/* Lock entries base address (1792 entries × 28 bytes) */
#define FILE_LOCK_ENTRIES_ADDR      0xE935CC

/* UID lock eventcount address */
#define FILE_UID_LOCK_EC_ADDR       0xE2C028

/* Secondary table address (58 words) */
#define FILE_LOCK_TABLE2_ADDR       0xEA3DC4
#endif

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/* Secondary lock table (58 words, purpose TBD) */
extern uint16_t FILE_$LOCK_TABLE2[];

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * OS_PROC_SHUTWIRED - Shutdown wired pages (called on access denial)
 *
 * Called when access rights check fails. Purpose is to release
 * any wired pages and resources.
 *
 * Parameters:
 *   status - Output status code
 *
 * Original address: 0x00E5D050
 */
extern void OS_PROC_SHUTWIRED(status_$t *status);

/*
 * FILE_$DELETE_INT - Internal delete handler
 *
 * Declared in file.h, used internally for refcount operations.
 */

/*
 * ============================================================================
 * Internal Locking Data Structures and Globals
 * ============================================================================
 */

/*
 * File lock entry structure (detailed)
 * Size: 28 bytes (0x1C)
 *
 * This is the complete structure for each lock entry.
 * Entries are stored at DAT_00e935b0 (base offset for entry N = N * 0x1C)
 */
typedef struct file_lock_entry_detail_t {
    uint32_t    context;        /* 0x00: Lock context (param_7/param_5) */
    uint32_t    node_low;       /* 0x04: Node address low (or local node info) */
    uint32_t    node_high;      /* 0x08: Node address high */
    uint32_t    uid_high;       /* 0x0C: File UID high part */
    uint32_t    uid_low;        /* 0x10: File UID low part */
    uint16_t    next;           /* 0x14: Next entry in chain (hash or free) */
    uint16_t    sequence;       /* 0x16: Lock sequence number */
    uint8_t     refcount;       /* 0x18: Reference count */
    uint8_t     flags1;         /* 0x19: Flags - bit 7=remote, bits 0-5=rights mask */
    uint8_t     rights;         /* 0x1A: Access rights mask */
    uint8_t     flags2;         /* 0x1B: Flags - bit 7=side, bits 3-6=lock mode,
                                         bit 2=remote flag, bit 1=pending, bit 0=? */
} file_lock_entry_detail_t;

/*
 * Lock entry flags (flags2 byte at offset 0x1B)
 */
#define FILE_LOCK_F2_SIDE       0x80    /* Lock side (0=reader, 1=writer) */
#define FILE_LOCK_F2_MODE_MASK  0x78    /* Lock mode (bits 3-6) */
#define FILE_LOCK_F2_MODE_SHIFT 3
#define FILE_LOCK_F2_REMOTE     0x04    /* Remote lock flag */
#define FILE_LOCK_F2_PENDING    0x02    /* Lock pending */
#define FILE_LOCK_F2_FLAG0      0x01    /* Unknown flag */

/*
 * Lock entry flags (flags1 byte at offset 0x19)
 */
#define FILE_LOCK_F1_REMOTE     0x80    /* Remote lock indicator */
#define FILE_LOCK_F1_RIGHTS     0x3F    /* Rights mask bits */

/*
 * Per-process lock table
 * Located at 0xE9F9CA (= FILE_LOCK_TABLE_ADDR - 2)
 * Each process (by ASID) has 300 bytes:
 *   - Bytes 0-1: Lock count at offset 0x1D98 (relative to base + ASID*2)
 *   - Bytes 2-299: Lock index array (up to 149 entries, 2 bytes each)
 */
#define FILE_PROC_LOCK_TABLE_ADDR   0xE9F9CA
#define FILE_PROC_LOCK_ENTRY_SIZE   300     /* 0x12C */
#define FILE_PROC_LOCK_MAX_ENTRIES  150     /* 0x96 entries per process */

/*
 * Lock hash table (array of head pointers)
 * Located at FILE_$LOCK_CONTROL + 0xC8
 */
extern uint16_t FILE_$LOT_HASHTAB[];

/*
 * External lock control variables (in file_lock_control_t)
 */

/* FILE_$LOT_FREE - Head of free lock entry list (at offset 0x2CE) */
/* Already declared in file.h */

/* Highest allocated lock entry index (at offset 0x2CC) */
extern uint16_t FILE_$LOT_HIGH;

/* Lock sequence counter (at offset 0x2C4) */
extern uint32_t FILE_$LOT_SEQN;

/* Lock mode compatibility tables */
extern uint16_t FILE_$LOCK_MODE_TABLE[];      /* At offset 0x58 (24 entries) */
extern uint16_t FILE_$LOCK_COMPAT_TABLE[];    /* At offset 0x28 (12 entries) */
extern uint16_t FILE_$LOCK_MAP_TABLE[];       /* At offset 0x40 (12 entries) */
extern uint16_t FILE_$LOCK_REQ_TABLE[];       /* At offset 0x88 (12 entries) */
extern uint16_t FILE_$LOCK_CVT_TABLE[];       /* At offset 0xA0 (12 entries) */
extern uint16_t FILE_$LOCK_ILLEGAL_MASK;      /* At offset 0x2C8 - illegal modes */
extern uint8_t  FILE_$LOT_FULL;               /* At offset 0x2D0 - table full flag */

/* ASID group mapping table (12 entries) - same address as LOCK_MAP_TABLE on m68k */
extern uint16_t FILE_$ASID_MAP[];

/* Default initial file size */
extern uint32_t FILE_$DEFAULT_SIZE;

/* Audit log enabled flag */
extern int8_t AUDIT_$ENABLED;

/* Network log enabled flag */
extern int8_t NETLOG_$OK_TO_LOG;

/* Current process ASID */
extern uint16_t PROC1_$AS_ID;

/*
 * ============================================================================
 * Internal Locking Functions
 * ============================================================================
 */

/*
 * FILE_$PRIV_LOCK - Core locking function
 *
 * Main internal function for all lock operations. Called by FILE_$LOCK,
 * FILE_$LOCK_D, FILE_$CHANGE_LOCK_D, and remote lock handlers.
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   asid         - Process ASID (from PROC1_$AS_ID)
 *   lock_index   - Lock table index (0 for new lock)
 *   lock_mode    - Lock mode (0-20)
 *   rights       - Rights byte (mode specific)
 *   flags        - Lock operation flags (see FILE_LOCK_FLAG_* constants)
 *   param_7      - Context parameter (0 for local locks)
 *   param_8      - Node address for remote locks
 *   param_9      - Additional context
 *   param_10     - Default address (usually &DAT_00e5e61e)
 *   param_11     - Additional flags
 *   lock_ptr_out - Output: lock context pointer
 *   result_out   - Output: result status
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E5F0EE
 */
void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t lock_index,
                     uint16_t lock_mode, int16_t rights, uint32_t flags,
                     int32_t param_7, uint32_t param_8, uint32_t param_9,
                     void *param_10, uint16_t param_11, uint32_t *lock_ptr_out,
                     uint16_t *result_out, status_$t *status_ret);

/*
 * FILE_$PRIV_UNLOCK - Core unlock function
 *
 * Main internal function for all unlock operations.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_index   - Lock table index (0 to search)
 *   mode_asid    - Combined: high word=lock_mode, low word=ASID
 *   remote_flags - Remote operation flags
 *   param_5      - Context high
 *   param_6      - Context low (node address)
 *   dtv_out      - Output: data-time-valid info
 *   status_ret   - Output: status code
 *
 * Returns:
 *   Result byte (modified flag)
 *
 * Original address: 0x00E5FD32
 */
uint8_t FILE_$PRIV_UNLOCK(uid_t *file_uid, uint16_t lock_index,
                          uint32_t mode_asid, int32_t remote_flags,
                          int32_t param_5, int32_t param_6,
                          uint32_t *dtv_out, status_$t *status_ret);

/*
 * FILE_$PRIV_UNLOCK_ALL - Unlock all locks for a process
 *
 * Releases all locks held by one or all processes.
 *
 * Parameters:
 *   asid_ptr - Pointer to ASID (0 = all processes)
 *
 * Original address: 0x00E60BD0
 */
void FILE_$PRIV_UNLOCK_ALL(uint16_t *asid_ptr);

/*
 * Internal lock info structure (34 bytes)
 * Output format for FILE_$LOCAL_READ_LOCK and FILE_$READ_LOCK_ENTRYI
 *
 * Note: This structure has 34 bytes total (8 longs + 1 short when copied).
 * The holder_node field at offset 0x16 is unaligned for 32-bit access,
 * so this structure should be packed on non-m68k architectures.
 *
 * For local locks (remote_flag=0):
 *   holder_node/port = NODE_$ME/ROUTE_$PORT (we are the holder)
 *   owner_node = entry.node_low (who locked it)
 *   remote_info = entry.node_high
 *
 * For remote locks (remote_flag=1):
 *   holder_node/port = entry.node_low/high (remote holder)
 *   owner_node = NODE_$ME (we are the owner)
 *   remote_info = ROUTE_$PORT
 */
typedef struct {
    uid_t    file_uid;      /* 0x00: File UID (8 bytes) */
    uint32_t context;       /* 0x08: Lock context */
    uint32_t owner_node;    /* 0x0C: Owner's node address (who initiated the lock) */
    uint16_t side;          /* 0x10: Lock side (0=reader, 1=writer) */
    uint16_t mode;          /* 0x12: Lock mode */
    uint16_t sequence;      /* 0x14: Lock sequence number */
    uint32_t holder_node;   /* 0x16: Lock holder's node (who actually holds it) */
    uint32_t holder_port;   /* 0x1A: Lock holder's port */
    uint32_t remote_info;   /* 0x1E: Remote node/port info (4 bytes, total=34) */
} file_lock_info_internal_t;

/*
 * FILE_$READ_LOCK_ENTRYI - Read lock entry by iteration
 *
 * Iterates through lock entries, returning info about each.
 *
 * Parameters:
 *   file_uid   - File UID to search for
 *   index      - Pointer to iteration index (starts at 1)
 *   info_out   - Output buffer for lock info
 *   status_ret - Output status code
 *
 * Original address: 0x00E6093C
 */
void FILE_$READ_LOCK_ENTRYI(uid_t *file_uid, uint16_t *index,
                             file_lock_info_internal_t *info_out, status_$t *status_ret);

/*
 * FILE_$READ_LOCK_ENTRYUI - Read lock entry by UID (unchecked)
 *
 * Reads lock entry info for a file without access checks.
 *
 * Parameters:
 *   file_uid   - File UID to search for
 *   info_out   - Output buffer for lock info
 *   status_ret - Output status code
 *
 * Original address: 0x00E6046E
 */
void FILE_$READ_LOCK_ENTRYUI(uid_t *file_uid, void *info_out, status_$t *status_ret);

/*
 * FILE_$LOCAL_READ_LOCK - Read local lock entry data
 *
 * Searches the local lock table for a lock on the specified file
 * and returns the lock information.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   info_out   - Output buffer for lock info (34 bytes)
 *   status_ret - Output status code
 *
 * Original address: 0x00E6050E
 */
void FILE_$LOCAL_READ_LOCK(uid_t *file_uid, file_lock_info_internal_t *info_out, status_$t *status_ret);

/*
 * Extended lock entry query structure
 * Passed from callers like FILE_$READ_LOCK_ENTRYUI
 * Contains both the file UID and process identification
 */
typedef struct {
    uid_t file_uid;      /* 0x00: File UID (8 bytes) */
    uint16_t side;       /* 0x10: Lock side (0=reader, 1=writer) from flags2 bit 7 */
    uint16_t asid;       /* 0x12: Process ASID to check */
} lock_verify_request_t;

/*
 * FILE_$LOCAL_LOCK_VERIFY - Verify local lock ownership
 *
 * Checks if the specified file is locked by the process identified
 * in the request structure.
 *
 * Parameters:
 *   request    - Lock verification request containing file UID and process info
 *   status_ret - Output status code
 *
 * Original address: 0x00E6081C
 */
void FILE_$LOCAL_LOCK_VERIFY(lock_verify_request_t *request, status_$t *status_ret);

/*
 * FILE_$VERIFY_LOCK_HOLDER - Verify lock holder is still valid
 *
 * Checks if the lock holder is still holding the lock. If the lock
 * has been released, cleans up the stale entry.
 *
 * Parameters:
 *   lock_info  - Lock information structure (from read operations)
 *   status_ret - Output status code
 *
 * Original address: 0x00E60732
 */
void FILE_$VERIFY_LOCK_HOLDER(file_lock_info_internal_t *lock_info, status_$t *status_ret);

/*
 * Helper: Audit lock/unlock operations
 * Called when AUDIT_$ENABLED is set
 *
 * Original address: 0x00E5E88A (renamed from FUN_*)
 */
void FILE_$AUDIT_LOCK(status_$t status, uid_t *file_uid, uint16_t lock_mode);

/*
 * ============================================================================
 * Internal Protection/ACL Functions
 * ============================================================================
 */

/*
 * FILE_$SET_PROT_INT - Set file protection (internal)
 *
 * Core internal function for setting file protection. Handles both local
 * and remote files, ACL checking, and locksmith privileges.
 *
 * Parameters:
 *   file_uid    - UID of file to modify
 *   acl_data    - ACL data buffer (44 bytes)
 *   attr_type   - Protection attribute type
 *   prot_type   - Protection type being set
 *   subsys_flag - Subsystem data flag (negative to allow override)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E5DD08
 */
void FILE_$SET_PROT_INT(uid_t *file_uid, void *acl_data, uint16_t attr_type,
                        uint16_t prot_type, int16_t subsys_flag,
                        status_$t *status_ret);

/*
 * FILE_$CHECK_SAME_VOLUME - Check if two files are on the same volume
 *
 * Checks if two file UIDs refer to objects on the same volume.
 * Used during protection operations to verify source/target locations.
 *
 * Parameters:
 *   file_uid1     - First file UID
 *   file_uid2     - Second file UID
 *   copy_location - If negative, copy location info on remote hit
 *   location_out  - Output buffer for location info (32 bytes)
 *   status_ret    - Output status code
 *
 * Returns:
 *   0: Files are on different volumes or error occurred
 *   -1: Files are local and on same logical volume
 *
 * Original address: 0x00E5E476
 */
int8_t FILE_$CHECK_SAME_VOLUME(uid_t *file_uid1, uid_t *file_uid2,
                                int8_t copy_location, uint32_t *location_out,
                                status_$t *status_ret);

/*
 * FILE_$AUDIT_SET_PROT - Log audit event for protection changes
 *
 * Logs a protection/ACL change audit event if auditing is enabled.
 *
 * Parameters:
 *   file_uid   - UID of file being modified
 *   acl_data   - ACL data buffer (44 bytes)
 *   prot_info  - Protection info (8 bytes)
 *   prot_type  - Protection type being set
 *   status     - Status of the operation
 *
 * Original address: 0x00E5DC96
 */
void FILE_$AUDIT_SET_PROT(uid_t *file_uid, void *acl_data, void *prot_info,
                          uint16_t prot_type, status_$t status);

/*
 * ============================================================================
 * External ACL Functions
 * ============================================================================
 */

/*
 * ACL_$SET_ACL_CHECK - Check ACL permissions for set operation
 *
 * Verifies the caller has permission to modify ACL.
 *
 * Original address: 0x00E470C4
 */
int8_t ACL_$SET_ACL_CHECK(uid_t *file_uid, void *acl_data, uid_t *source_uid,
                          int16_t *prot_type, int8_t *permission_flags,
                          status_$t *status_ret);

/*
 * ACL_$GET_LOCAL_LOCKSMITH - Check if local locksmith mode is enabled
 *
 * Returns:
 *   0 if locksmith mode is enabled (has privileges)
 *   Non-zero otherwise
 *
 * Original address: 0x00E4923C
 */
int16_t ACL_$GET_LOCAL_LOCKSMITH(void);

/*
 * ACL_$CONVERT_FUNKY_ACL - Convert "funky" ACL format
 *
 * Converts an ACL UID in the "funky" format to standard ACL components.
 *
 * Original address: 0x00E4900C
 */
void ACL_$CONVERT_FUNKY_ACL(void *acl_uid, void *acl_data_out,
                             void *prot_info_out, void *target_uid_out,
                             status_$t *status_ret);

/*
 * ACL_$DEF_ACLDATA - Get default ACL data
 *
 * Fills in default ACL data using nil user/group/org UIDs.
 *
 * Original address: 0x00E478DC
 */
void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out);

/*
 * NOTE: AST functions (AST_$GET_DISM_SEQN, AST_$GET_COMMON_ATTRIBUTES, etc.)
 * are declared in ast/ast.h which is included above.
 */

/*
 * NOTE: REM_FILE_$* functions are declared in rem_file/rem_file.h
 * which is included above. No need to redeclare them here.
 */

#endif /* FILE_INTERNAL_H */
