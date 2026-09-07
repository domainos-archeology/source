/*
 * NAME - Pathname and Naming Services Module
 *
 * The NAME subsystem handles pathname resolution, directory management,
 * and naming services in Domain/OS. It provides functions for:
 *   - Pathname validation and resolution
 *   - Working directory (wdir) and naming directory (ndir) management
 *   - Remote naming operations (REM_NAME_$*)
 *   - File creation and ACL operations
 *
 * Path Types:
 *   - Relative paths: "foo/bar"
 *   - Absolute paths: "/foo/bar" (from root)
 *   - Network paths: "//node/path" (cross-node)
 *   - Node data paths: "`node_data/..." (node-specific data)
 */

#ifndef NAME_H
#define NAME_H

#include "base/base.h"

/* Maximum pathname length */
#define NAME_$MAX_PNAME_LEN   256

/*
 * Path type constants
 * Returned by NAME_$VALIDATE to indicate what kind of path was parsed.
 */
typedef enum {
    start_path_$error     = 0,   /* Invalid/too long path */
    start_path_$relative  = 1,   /* Relative path (no leading /) */
    start_path_$absolute  = 3,   /* Absolute path (starts with /) */
    start_path_$network   = 4,   /* Network path (starts with //) */
    start_path_$node_data = 5    /* Node data path (starts with `node_data) */
} start_path_type_t;

/*
 * ============================================================================
 * Status codes for the naming server (module 0x0E)
 *
 * This is the single home for every 0x000Exxxx status code; dir/ and file/
 * used to carry `#ifndef`-guarded copies of most of them with two different
 * spellings and, in one case, two different values (source-pp31).
 *
 * The text after each code is the entry in the Domain/OS 10.4 status-code
 * database (~/src/domainos-archeology/stcodes, stcode.db.10.4), and every
 * identifier below is now that text slugged - source-tiil replaced eight
 * inherited names that sat on the wrong wording.  The values never moved;
 * they are what the binary stores.  Two of the replaced names were extra
 * spellings of 0x000E0016 and are now spelled status_$naming_directory_locked
 * at every call site (dir/do_op.c, dir/get_entry_cached.c, dir/lock_obj.c,
 * rem_file/server.c).
 *
 * Codes this port has never needed and therefore does not define: 0x000E0002,
 * 0x000E0003, 0x000E000C, 0x000E0010 ("name is not a file"), 0x000E0013,
 * 0x000E0015, 0x000E0017, 0x000E0018, 0x000E001B, 0x000E0021, 0x000E0024,
 * 0x000E0026..0x000E002A, 0x000E002C, 0x000E0034 ("ran out of address
 * space"), 0x000E0035.  Add them from the database, never by guessing.
 * ============================================================================
 */
#define status_$naming_invalid_pathname                     0x000e0004  /* invalid pathname */
#define status_$naming_invalid_link                         0x000e0005  /* invalid link */
#define status_$naming_not_a_link                           0x000e0006  /* not a link */
#define status_$naming_name_not_found                       0x000e0007  /* name not found */
#define status_$naming_invalid_link_operation               0x000e000a  /* invalid link operation */
#define status_$naming_invalid_leaf                         0x000e000b  /* invalid leaf */
#define status_$naming_bad_directory                        0x000e000d  /* bad directory */
#define status_$naming_branch_is_not_a_directory            0x000e000e  /* branch is not a directory */
#define status_$naming_directory_not_empty                  0x000e000f  /* directory is not empty */
#define status_$naming_illegal_directory_operation          0x000e0011  /* illegal directory operation */
#define status_$naming_bad_type                             0x000e0012  /* bad type */
#define status_$naming_insufficient_rights                  0x000e0014  /* insufficient rights */
#define status_$naming_directory_locked                     0x000e0016  /* directory is in use (locked) */
#define status_$naming_cannot_find_entry_in_replicated_root 0x000e0019  /* cannot find entry in replicated root */
#define status_$naming_last_entry_in_replicated_root_returned 0x000e001a /* last entry in replicated root returned */
#define status_$naming_helper_sent_packets_with_errors      0x000e001c  /* name server helper sent packet with errors */
#define status_$naming_cant_find_name_server_helper         0x000e001e  /* cant find name server helper */
#define status_$naming_directory_must_be_root               0x000e001f  /* directory must be root */
#define status_$naming_directory_not_found_in_pathname      0x000e0020  /* directory not found in pathname */
#define status_$naming_entry_stale                          0x000e0022  /* cache entry is stale */
#define status_$naming_entry_repaired                       0x000e0023  /* cache entry was stale and was updated */
#define status_$naming_internal_error                       0x000e0025  /* internal error */
#define status_$naming_directory_not_local                  0x000e002b  /* directory not local */
#define status_$naming_leaf_truncated                       0x000e002d  /* leaf truncated */
/* 0x000E002E was spelled status_$naming_object_is_not_an_acl_object here,
 * which made every dir/ TU see 0x2E for a name whose value is 0x2F.  The
 * binary stores 0xE002E for the short-buffer cases (0x00E4D704 in
 * DIR_$READ_LINKU, 0x00E4E40A in DIR_$DIR_READU) and 0xE002F for the ACL
 * case (0x00E564A2 in DIR_$OLD_SET_DEFAULT_ACL). */
#define status_$naming_bad_buffer_size                      0x000e002e  /* bad buffer size */
#define status_$naming_object_is_not_an_acl_object          0x000e002f  /* object is not an acl object */
#define status_$naming_vol_mounted_read_only                0x000e0030  /* volume has been mounted read-only */
#define status_$naming_cant_recovery_dir_on_ro_vol          0x000e0031  /* write-protected volume prevents recovery of a damaged directory */
#define status_$naming_too_many_hard_links                  0x000e0032  /* too many hard links exist to file */
#define status_$naming_directory_object_not_found           0x000e0033  /* directory object not found */

/*
 * Cached directory mapping info (16 bytes)
 *
 * Holds the MST mapping state of a directory that is kept mapped for fast
 * access (see name_$map_dir / name_$unmap_dir_buffers).  The directory is
 * mapped as two 0x8000-byte halves; when they are contiguous, second_base
 * == first_base + 0x8000.
 */
typedef struct name_$mapped_info_t {
    int8_t      active;         /* +0x00: negative (0xFF) when the mapping is active */
    uint8_t     pad_01;         /* +0x01 */
    uint16_t    reserved_02;    /* +0x02: set to 0 by name_$map_dir; must be 0 to reuse */
    uint32_t    first_base;     /* +0x04: mapped base address (MST_$MAPS A0 result) */
    uint16_t    reserved_08;    /* +0x08 */
    uint16_t    entry_count;    /* +0x0A: set to 1 by name_$map_dir; must be 1 to reuse */
    uint32_t    second_base;    /* +0x0C: first_base + 0x8000 */
} name_$mapped_info_t;

/*
 * Number of per-address-space slots in the NAME data area.
 * NAME_$INIT initialises 0x3A (58) slots (moveq #0x39 / dbf).
 */
#define NAME_$MAX_ASIDS     58

/*
 * NAME data area (0xE80264, 0xB20 bytes)
 *
 * All the well-known UIDs, the per-ASID working/naming directory UIDs and
 * the cached mapping info blocks live in one contiguous block; the original
 * code addresses everything relative to 0xE80264.
 *
 * Original m68k addresses:
 *   NAME_$NODE_DATA_UID:    0xE80264 (+0x000)
 *   NAME_$COM_MAPPED_INFO:  0xE8026C (+0x008)
 *   NAME_$COM_UID:          0xE8027C (+0x018)
 *   NAME_$NODE_MAPPED_INFO: 0xE80284 (+0x020)
 *   NAME_$NODE_UID:         0xE80294 (+0x030)
 *   NAME_$ROOT_UID:         0xE8029C (+0x038)
 *   NAME_$NDIR_MAPPED_INFO: 0xE802A4 (+0x040) [58 x 16 bytes]
 *   NAME_$NDIR_UID:         0xE80644 (+0x3E0) [58 x 8 bytes]
 *   NAME_$WDIR_MAPPED_INFO: 0xE80814 (+0x5B0) [58 x 16 bytes]
 *   NAME_$WDIR_UID:         0xE80BB4 (+0x950) [58 x 8 bytes]
 */
typedef struct name_$data_t {
    uid_t               node_data_uid;                      /* +0x000 */
    name_$mapped_info_t com_mapped_info;                    /* +0x008 */
    uid_t               com_uid;                            /* +0x018 */
    name_$mapped_info_t node_mapped_info;                   /* +0x020 */
    uid_t               node_uid;                           /* +0x030 */
    uid_t               root_uid;                           /* +0x038 */
    name_$mapped_info_t ndir_mapped_info[NAME_$MAX_ASIDS];  /* +0x040 */
    uid_t               ndir_uid[NAME_$MAX_ASIDS];          /* +0x3E0 */
    name_$mapped_info_t wdir_mapped_info[NAME_$MAX_ASIDS];  /* +0x5B0 */
    uid_t               wdir_uid[NAME_$MAX_ASIDS];          /* +0x950 */
} name_$data_t;

extern name_$data_t NAME_$DATA;     /* 0xE80264 */

/*
 * Well-known UIDs managed by the NAME subsystem
 *
 * These are the historical symbol names; they resolve to the fields of
 * NAME_$DATA so that the data layout stays identical to the original.
 */
#define NAME_$NODE_DATA_UID     (NAME_$DATA.node_data_uid)  /* Node data directory UID */
#define NAME_$COM_UID           (NAME_$DATA.com_uid)        /* /com directory UID */
#define NAME_$NODE_UID          (NAME_$DATA.node_uid)       /* This node's directory UID */
#define NAME_$ROOT_UID          (NAME_$DATA.root_uid)       /* Root directory UID */

extern uid_t NAME_$CANNED_REP_ROOT_UID;
extern uid_t NAME_$CANNED_ROOT_UID; /* Canned root UID (for fallback), 0xE173E4 */

/*
 * Per-address-space working/naming directory UIDs
 *
 * These are arrays indexed by PROC1_$AS_ID.  The historical names refer to
 * element 0 (the array base), matching the original byte-offset addressing.
 */
#define NAME_$WDIR_UID          (NAME_$DATA.wdir_uid[0])    /* Working directory UID array base */
#define NAME_$NDIR_UID          (NAME_$DATA.ndir_uid[0])    /* Naming directory UID array base */

/*
 * Cached mapping info for well-known directories
 *
 * NODE and COM have a single global mapping.
 * WDIR and NDIR are per-address-space (indexed by PROC1_$AS_ID).
 */
#define NAME_$NODE_MAPPED_INFO  (NAME_$DATA.node_mapped_info)
#define NAME_$COM_MAPPED_INFO   (NAME_$DATA.com_mapped_info)
#define NAME_$WDIR_MAPPED_INFO  (NAME_$DATA.wdir_mapped_info[0])
#define NAME_$NDIR_MAPPED_INFO  (NAME_$DATA.ndir_mapped_info[0])

/*
 * Constants living in the NAME code region that are passed by reference
 * (Pascal VAR parameters) by NAME and DIR routines.
 */
extern uint8_t DAT_00e54730;    /* 0xE54730: 4 zero bytes just before NAME_$UNLOCK_DIR;
                                   FILE_$PRIV_LOCK param_10 / FILE_$TRUNCATE length arg.
                                   Ghidra label: NAME_$CONST_ZERO_L */
extern uint8_t DAT_00e54b28;    /* 0xE54B28: ACL_$RIGHTS parameter just after NAME_$LOCK_DIR.
                                   Ghidra label: NAME_$CONST_ZERO_L2 */
extern int16_t NAME_$CONST_ZERO_W; /* 0xE5472E: shared literal zero word.  Roles seen in the
                                   code: TIME_$WAIT delay type 0 (relative) at 0xE54940 and
                                   ACL_$RIGHTS option_flags 0 at 0xE56FC2/0xE5704A.  It was
                                   previously mislabelled ACL_TYPE_FILE. */
extern int16_t ACL_TYPE_DIR;    /* 0xE54B26: literal word 1 - ACL object type (directory) */

/*
 * ============================================================================
 * Per-process directory-lock state (NAME/DIR module A5 data area)
 * ============================================================================
 *
 * NAME_$LOCK_DIR, NAME_$UNLOCK_DIR and the DIR_$OLD_* entry points all run
 * with A5 = 0xE7FD24 (`lea (0xe7fd24).l,A5` in every gate) and address this
 * state as (offset,A5).  All four tables are indexed *directly* by
 * PROC1_$CURRENT - the code applies no 1-based adjustment - and each holds
 * NAME_$MAX_LOCK_PROCS entries; DIR_$OLD_INIT (0xE314F4) clears 0x3A == 58
 * NAME_$LOCK_UID entries with `moveq #0x39` + `dbf`.
 */
#define NAME_$MAX_LOCK_PROCS    58

extern uint32_t NAME_$LOCK_SLOT[NAME_$MAX_LOCK_PROCS];   /* A5+0x03C = 0xE7FD60: FILE_$PRIV_LOCK slot */
extern int16_t  NAME_$LOCK_MODE[NAME_$MAX_LOCK_PROCS];   /* A5+0x13E = 0xE7FE62: lock mode in effect */
extern uint32_t NAME_$LOCK_HANDLE[NAME_$MAX_LOCK_PROCS]; /* A5+0x1BC = 0xE7FEE0: mapped directory base */
extern uid_t    NAME_$LOCK_UID[NAME_$MAX_LOCK_PROCS];    /* A5+0x2B8 = 0xE7FFDC: UID of the locked dir */

/*
 * Directory handles
 *
 * NAME_$LOCK_DIR hands back the virtual address at which the directory is
 * mapped and stores it in a 32-bit word, because m68k pointers are 32 bits
 * wide.  Turning that word back into a pointer (0xE54B06:
 * `movea.l (A0),A1 ; cmpi.w #0x1,(A1)`) is the one architecture-specific step
 * in the routine.  On m68k it is the identity cast the original performs; a
 * host whose pointers are wider supplies a translation (name/handle_map.c)
 * instead, so the code can be exercised without a 32-bit address space.
 *
 * dir/ uses the same pair for the directory handle dir_$add_entry receives at
 * A6+0x08 and passes down through dir_insert_ctx_t.handle.
 */
#if defined(ARCH_M68K)
#define NAME_$HANDLE_TO_PTR(h)   ((void *)(uintptr_t)(h))
#define NAME_$PTR_TO_HANDLE(p)   ((uint32_t)(uintptr_t)(p))
#else
void    *name_$handle_to_ptr(uint32_t handle);
uint32_t name_$ptr_to_handle(const void *ptr);
#define NAME_$HANDLE_TO_PTR(h)   name_$handle_to_ptr(h)
#define NAME_$PTR_TO_HANDLE(p)   name_$ptr_to_handle(p)
#endif

/* ============================================================================
 * Public Function Prototypes
 * ============================================================================ */

/*
 * NAME_$INIT - Initialize the naming subsystem
 *
 * Called during system boot to initialize naming services.
 * If vol_root_uid is NIL, retrieves root/node UIDs from the boot volume VTOC.
 * Otherwise uses the provided UIDs directly.
 *
 * Parameters:
 *   vol_root_uid - Root directory UID (or NIL to auto-detect)
 *   vol_node_uid - Node directory UID (or NIL to auto-detect)
 *
 * Original address: 0x00e31624
 */
void NAME_$INIT(uid_t *vol_root_uid, uid_t *vol_node_uid);

/*
 * NAME_$VALIDATE - Validate a pathname and determine its type
 *
 * Checks pathname length (must be <= 256) and determines the path type
 * (relative, absolute, network, or node_data).
 *
 * Parameters:
 *   path           - The pathname to validate
 *   path_len       - Pointer to pathname length (Pascal string style)
 *   consumed       - Output: number of leading characters consumed (/, //, etc.)
 *   start_path_type - Output: the determined path type
 *
 * Returns:
 *   0xFF on success (always returns success, sets start_path_type to error if invalid)
 *
 * Original address: 0x00e49f4c
 */
boolean NAME_$VALIDATE(char *path, uint16_t *path_len, int16_t *consumed,
                       start_path_type_t *start_path_type);

/*
 * NAME_$RESOLVE - Resolve a pathname to a UID
 *
 * Converts a pathname string to the UID of the named object.
 *
 * Parameters:
 *   path         - The pathname to resolve
 *   path_len     - Pointer to pathname length
 *   resolved_uid - Output: UID of the resolved object
 *   status_ret   - Output: status code
 *
 * Original address: 0x00e4a258
 */
void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid, status_$t *status_ret);

/*
 * NAME_$DROP - Drop/delete a named object
 *
 * Removes a named entry from its parent directory.
 *
 * Parameters:
 *   path       - Pathname of object to drop
 *   path_len   - Pointer to pathname length
 *   file_uid   - UID of the file (for verification)
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a2b8
 */
void NAME_$DROP(char *path, int16_t *path_len, uid_t *file_uid, status_$t *status_ret);

/*
 * NAME_$CR_FILE - Create a file with the given pathname
 *
 * Creates a new file by resolving the parent directory, creating
 * the file object, copying ACLs, and adding the directory entry.
 *
 * Parameters:
 *   path       - Pathname for the new file
 *   path_len   - Pointer to pathname length
 *   file_ret   - Output: UID of created file (or NIL on failure)
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a316
 */
void NAME_$CR_FILE(char *path, int16_t *path_len, uid_t *file_ret, status_$t *status_ret);

/*
 * NAME_$SET_WDIR - Set working directory
 *
 * Original address: 0x00e4a3d0
 */
void NAME_$SET_WDIR(char *path, int16_t *path_len, status_$t *status_ret);

/*
 * NAME_$SET_WDIRUS - Set working directory (using UID)
 *
 * Original address: 0x00e58670
 */
void NAME_$SET_WDIRUS(uid_t *dir_uid, status_$t *status_ret);

/*
 * NAME_$SET_NDIRUS - Set naming directory (using UID)
 *
 * Original address: 0x00e587a0
 */
void NAME_$SET_NDIRUS(uid_t *dir_uid, status_$t *status_ret);

/*
 * NAME_$GET_WDIR_UID - Get working directory UID
 *
 * Original address: 0x00e58960
 */
void NAME_$GET_WDIR_UID(uid_t *wdir_uid);

/*
 * NAME_$GET_NDIR_UID - Get naming directory UID
 *
 * Original address: 0x00e5898e
 */
void NAME_$GET_NDIR_UID(uid_t *ndir_uid);

/*
 * NAME_$GET_ROOT_UID - Get root directory UID
 *
 * Original address: 0x00e589bc
 */
void NAME_$GET_ROOT_UID(uid_t *root_uid);

/*
 * NAME_$GET_NODE_UID - Get node directory UID
 *
 * Original address: 0x00e589de
 */
void NAME_$GET_NODE_UID(uid_t *node_uid);

/*
 * NAME_$GET_NODE_DATA_UID - Get node data directory UID
 *
 * Original address: 0x00e58a00
 */
void NAME_$GET_NODE_DATA_UID(uid_t *node_data_uid);

/*
 * NAME_$GET_CANNED_ROOT_UID - Get canned root UID
 *
 * Original address: 0x00e58a20
 */
void NAME_$GET_CANNED_ROOT_UID(uid_t *canned_root_uid);

/*
 * ============================================================================
 * Old-style (pre-B-tree) name helpers used by the dir subsystem
 * ============================================================================
 */

/* name_$old_add_link - Add link with remote/local handling
 * Shared add entry helper for DIR_$OLD_ADDU and DIR_$OLD_ADD_HARD_LINKU.
 * Original address: 0x00E5674C (name/old_add_link.c)
 */
void name_$old_add_link(uid_t *dir_uid, char *name, uint16_t name_len,
                        uid_t *file_uid, uint8_t hard_link_flag,
                        status_$t *status_ret);

/* name_$old_get_root_entry - Root directory entry lookup
 * Original address: 0x00E57F74 (name/old_get_root_entry.c)
 */
void name_$old_get_root_entry(uid_t *dir_uid, char *name, uint16_t name_len,
                              void *entry_ret, status_$t *status_ret);

/* name_$old_get_entry_nonroot - Non-root directory entry lookup
 * Original address: 0x00E57CE0 (name/old_get_entry_nonroot.c)
 */
void name_$old_get_entry_nonroot(uid_t *dir_uid, char *name, uint16_t name_len,
                                 void *entry_ret, status_$t *status_ret);

/* name_$old_add_entry - Name-level add directory entry
 * Original address: 0x00E56682 (name/old_add_entry.c)
 */
void name_$old_add_entry(uid_t *dir_uid, uint16_t type, char *name,
                         uint16_t name_len, uid_t *file_uid,
                         uint32_t flags, status_$t *status_ret);

/* name_$old_drop_entry - Find and remove directory entry by name
 * type is the lock mode (low word of NAME_$LOCK_DIR flags; callers pass 0).
 * Original address: 0x00E56A04 (name/old_drop_entry.c)
 */
void name_$old_drop_entry(uid_t *dir_uid, char *name, uint16_t name_len,
                          uint16_t type, void *result, status_$t *status_ret);

/* name_$validate_leaf - Validate and parse leaf name
 * Returns negative (true) on success, non-negative on failure.
 * Original address: 0x00E54414 (name/validate_leaf.c)
 */
int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len);

/*
 * NAME_$LOCK_DIR - Enter super mode / acquire directory lock
 *
 * Parameters (5, not 4: the two words at (0x10,A6) and (0x12,A6) are separate
 * Pascal parameters even though every caller pushes them with a single
 * `move.l #imm,-(SP)`, e.g. 0x00040002 => lock_mode = 4, acl_rights = 2):
 *   dir_uid    - UID of the directory to lock
 *   handle_ret - Output: mapped base address of the directory
 *   lock_mode  - FILE_$PRIV_LOCK lock mode (high word of the pushed longword)
 *   acl_rights - required ACL rights; 0 skips the ACL_$RIGHTS check
 *                (low word of the pushed longword)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E54854
 */
void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret);

/*
 * NAME_$UNLOCK_DIR - Release directory lock / exit super mode
 *
 * Original address: 0x00E54734
 */
void NAME_$UNLOCK_DIR(status_$t *status_ret);

/*
 * NAME_CONVERT_ACL_STATUS - Convert an ACL status code to a naming status
 *
 * Original address: 0x00E5861C
 */
void NAME_CONVERT_ACL_STATUS(status_$t *status_ret);

/*
 * NAME_$SET_ACL - Set ACL on a named object
 *
 * Parameters:
 *   uid        - UID of the object
 *   acl        - ACL data to apply
 *   status_ret - Output: status code
 *
 * Original address: 0x00e58656
 */
void NAME_$SET_ACL(uid_t *uid, void *acl, status_$t *status_ret);

/*
 * NAME_$READ_DIRS_PS - Read directory entries (Pascal string)
 *
 * Original address: 0x00e588be
 */
void NAME_$READ_DIRS_PS(void);

/*
 * NAME_$CLEANUP - Clean up naming resources
 *
 * Original address: 0x00e5860e
 */
void NAME_$CLEANUP(void);

/*
 * NAME_$INIT_ASID - Initialize naming for an ASID (address space)
 *
 * Parameters:
 *   new_asid   - Pointer to the new address space ID
 *   status_ret - Output: status code
 *
 * Original address: 0x00e73cfc
 */
void NAME_$INIT_ASID(int16_t *new_asid, status_$t *status_ret);

/*
 * NAME_$FORK - Fork naming state to child process
 *
 * Parameters:
 *   parent_asid - Pointer to parent address space ID
 *   child_asid  - Pointer to child address space ID
 *
 * Original address: 0x00e73e44
 */
void NAME_$FORK(int16_t *parent_asid, int16_t *child_asid);

/*
 * NAME_$FREE_ASID - Free naming resources for an ASID
 *
 * Parameters:
 *   asid - Pointer to the address space ID to free
 *
 * Original address: 0x00e74da8
 */
void NAME_$FREE_ASID(int16_t *asid);

/*
 * NAMEQ - Compare two Pascal-style strings for equality
 *
 * Compares strings ignoring trailing spaces. Used internally for
 * pathname component matching.
 *
 * Parameters:
 *   str1     - First string
 *   len1     - Pointer to length of first string
 *   str2     - Second string
 *   len2     - Pointer to length of second string
 *
 * Returns:
 *   0xFF (true) if strings match, 0 (false) otherwise
 *
 * Original address: 0x00e49eac
 */
boolean NAMEQ(char *str1, uint16_t *len1, char *str2, uint16_t *len2);

/* ============================================================================
 * Remote Naming Functions (REM_NAME_$*)
 *
 * These functions handle distributed naming operations across Apollo network
 * nodes. They communicate with remote naming servers via PKT_$SAR_INTERNET.
 *
 * There are two categories of functions:
 * 1. Low-level functions that take explicit net/node parameters
 * 2. High-level wrappers that auto-locate a server and retry on failure
 * ============================================================================ */

/*
 * REM_NAME_SERVER_LOCAL - Check if naming server is on local node
 *
 * Returns:
 *   true (0xFF) if local server, false (0) if remote
 *
 * Original address: 0x00e4a408
 */
boolean REM_NAME_SERVER_LOCAL(void);

/*
 * REM_NAME_$REGISTER_SERVER - Register contact with naming server
 *
 * Updates the last-heard-from timestamp and sets the server contacted flag.
 *
 * Original address: 0x00e4a4ae
 */
void REM_NAME_$REGISTER_SERVER(void);

/*
 * REM_NAME_$GET_ENTRY_BY_NAME - Look up entry by name (low-level)
 *
 * Parameters:
 *   net        - Network ID (0 for local)
 *   node       - Node ID of naming server
 *   dir_uid    - UID of directory to search
 *   name       - Name to look up
 *   name_len   - Length of name (max 32)
 *   entry_ret  - Output: directory entry structure
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a588
 */
void REM_NAME_$GET_ENTRY_BY_NAME(uint32_t net, uint32_t node, uid_t *dir_uid,
                                  char *name, uint16_t name_len,
                                  void *entry_ret, status_$t *status_ret);

/*
 * REM_NAME_$GET_INFO - Get info about a named object (low-level)
 *
 * Parameters:
 *   net        - Network ID
 *   node       - Node ID of naming server
 *   uid        - UID of object to query
 *   info_ret   - Output: 22 bytes of info data
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a690
 */
void REM_NAME_$GET_INFO(uint32_t net, uint32_t node, uid_t *uid,
                        void *info_ret, status_$t *status_ret);

/*
 * REM_NAME_$LOCATE_SERVER - Locate a naming server
 *
 * First tries local node if server is local, then broadcasts.
 *
 * Parameters:
 *   node_ret   - Output: server node ID
 *   net_ret    - Output: server network ID (0 if local)
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a722
 */
void REM_NAME_$LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret,
                              status_$t *status_ret);

/*
 * REM_NAME_$GET_ENTRY_BY_NODE_ID - Look up entry by node ID (low-level)
 *
 * Parameters:
 *   net         - Network ID
 *   node        - Node ID of naming server
 *   dir_uid     - UID of directory to search
 *   target_node - Node ID to look up
 *   entry_ret   - Output: directory entry structure
 *   status_ret  - Output: status code
 *
 * Original address: 0x00e4a800
 */
void REM_NAME_$GET_ENTRY_BY_NODE_ID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                     uint32_t target_node, void *entry_ret,
                                     status_$t *status_ret);

/*
 * REM_NAME_$GET_ENTRY_BY_UID - Look up entry by UID (low-level)
 *
 * Parameters:
 *   net        - Network ID
 *   node       - Node ID of naming server
 *   dir_uid    - UID of directory to search
 *   target_uid - UID to look up
 *   entry_ret  - Output: directory entry structure
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a8cc
 */
void REM_NAME_$GET_ENTRY_BY_UID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                 uid_t *target_uid, void *entry_ret,
                                 status_$t *status_ret);

/*
 * REM_NAME_$READ_DIR - Read directory entries (low-level)
 *
 * Parameters:
 *   net         - Network ID
 *   node        - Node ID of naming server
 *   dir_uid     - UID of directory to read
 *   start_index - Index of first entry to read
 *   entries_ret - Output: array of directory entries (0x30 bytes each)
 *   max_entries - Maximum entries to return
 *   count_ret   - Output: actual number of entries returned
 *   status_ret  - Output: status code
 *
 * Original address: 0x00e4a984
 */
void REM_NAME_$READ_DIR(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint32_t start_index, void *entries_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret);

/*
 * REM_NAME_$READ_REP - Read replication information (low-level)
 *
 * Parameters:
 *   net         - Network ID
 *   node        - Node ID of naming server
 *   dir_uid     - UID of directory
 *   start_index - Index of first replica entry
 *   rep_ret     - Output: array of replica entries (0x12 bytes each)
 *   max_entries - Maximum entries to return
 *   count_ret   - Output: actual number of entries returned
 *   status_ret  - Output: status code
 *
 * Original address: 0x00e4ab44
 */
void REM_NAME_$READ_REP(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint32_t start_index, void *rep_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret);

/*
 * REM_NAME_$DIR_READU - Read directory entries (high-level with auto-locate)
 *
 * Automatically locates a server and retries on failure.
 *
 * Parameters:
 *   dir_uid      - UID of directory to read
 *   entries_ret  - Output: array of directory entries
 *   continuation - In/Out: continuation token (0 = start fresh)
 *   max_entries  - Pointer to max entries to return
 *   count_ret    - Output: actual number of entries returned
 *   status_ret   - Output: status code
 *
 * Original address: 0x00e4ac2c
 */
void REM_NAME_$DIR_READU(uid_t *dir_uid, void *entries_ret, int32_t *continuation,
                         uint16_t *max_entries, uint16_t *count_ret,
                         status_$t *status_ret);

/*
 * REM_NAME_$GET_ENTRY - Get directory entry (high-level with auto-locate)
 *
 * Automatically locates a server and retries on failure.
 *
 * Parameters:
 *   dir_uid    - UID of directory to search
 *   name       - Name to look up
 *   name_len   - Pointer to name length
 *   entry_ret  - Output: directory entry structure
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4ad18
 */
void REM_NAME_$GET_ENTRY(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret);

/*
 * REM_NAME_$FIND_NETWORK - Find network entry by node ID (high-level)
 *
 * Automatically locates a server and retries on failure.
 *
 * Parameters:
 *   dir_uid     - UID of directory to search
 *   target_node - Pointer to node ID to look up
 *   entry_ret   - Output: directory entry structure
 *   status_ret  - Output: status code
 *
 * Original address: 0x00e4add6
 */
void REM_NAME_$FIND_NETWORK(uid_t *dir_uid, uint32_t *target_node,
                            void *entry_ret, status_$t *status_ret);

/*
 * REM_NAME_$FIND_UID - Find object by UID (high-level with auto-locate)
 *
 * Automatically locates a server and retries on failure.
 *
 * Parameters:
 *   dir_uid    - UID of directory to search
 *   target_uid - UID to look up
 *   entry_ret  - Output: directory entry structure
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4ae84
 */
void REM_NAME_$FIND_UID(uid_t *dir_uid, uid_t *target_uid,
                        void *entry_ret, status_$t *status_ret);

#endif /* NAME_H */
