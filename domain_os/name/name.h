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
 * Status codes for naming operations (module 0x0E)
 */
#define status_$naming_invalid_pathname                0x000e0004
#define status_$naming_name_not_found                  0x000e0007
#define status_$naming_invalid_leaf                    0x000e000b
#define status_$naming_bad_directory                   0x000e000d
#define status_$naming_directory_not_found_in_pathname 0x000e0020

/*
 * Well-known UIDs managed by the NAME subsystem
 */
extern uid_t NAME_$ROOT_UID;        /* Root directory UID */
extern uid_t NAME_$NODE_UID;        /* This node's directory UID */
extern uid_t NAME_$NODE_DATA_UID;   /* Node data directory UID */
extern uid_t NAME_$COM_UID;         /* /com directory UID */
extern uid_t NAME_$CANNED_ROOT_UID; /* Canned root UID (for fallback) */

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
 * nodes. They communicate with remote naming servers.
 * ============================================================================ */

/*
 * REM_NAME_$REGISTER_SERVER - Register contact with naming server
 * Original address: 0x00e4a4ae
 */
void REM_NAME_$REGISTER_SERVER(void);

/*
 * REM_NAME_$GET_ENTRY_BY_NAME - Look up entry by name
 * Original address: 0x00e4a588
 */
void REM_NAME_$GET_ENTRY_BY_NAME(void *param_1, void *param_2, uid_t *dir_uid,
                                  char *name, uint16_t name_len,
                                  void *entry_ret, status_$t *status_ret);

/*
 * REM_NAME_$GET_INFO - Get info about a named object
 * Original address: 0x00e4a690
 */
void REM_NAME_$GET_INFO(void *param_1, void *param_2, uid_t *uid,
                        void *info_ret, status_$t *status_ret);

/*
 * REM_NAME_$LOCATE_SERVER - Locate a naming server
 * Original address: 0x00e4a722
 */
void REM_NAME_$LOCATE_SERVER(void *param_1, void *param_2, uid_t *uid,
                              void *server_ret, status_$t *status_ret);

/*
 * REM_NAME_$GET_ENTRY_BY_NODE_ID - Look up entry by node ID
 * Original address: 0x00e4a800
 */
void REM_NAME_$GET_ENTRY_BY_NODE_ID(void *param_1, void *param_2,
                                     uint32_t node_id, void *entry_ret,
                                     status_$t *status_ret);

/*
 * REM_NAME_$GET_ENTRY_BY_UID - Look up entry by UID
 * Original address: 0x00e4a8cc
 */
void REM_NAME_$GET_ENTRY_BY_UID(void *param_1, void *param_2, uid_t *uid,
                                 void *entry_ret, status_$t *status_ret);

/*
 * REM_NAME_$READ_DIR - Read directory entries
 * Original address: 0x00e4a984
 */
void REM_NAME_$READ_DIR(void *param_1, void *param_2, uid_t *dir_uid,
                        void *entries_ret, int16_t *count_ret,
                        status_$t *status_ret);

/*
 * REM_NAME_$READ_REP - Read replication information
 * Original address: 0x00e4ab44
 */
void REM_NAME_$READ_REP(void *param_1, void *param_2, uid_t *uid,
                        void *rep_ret, status_$t *status_ret);

/*
 * REM_NAME_$DIR_READU - Read directory (unsigned)
 * Original address: 0x00e4ac2c
 */
void REM_NAME_$DIR_READU(void *param_1, void *param_2, uid_t *dir_uid,
                         void *entry_ret, status_$t *status_ret);

/*
 * REM_NAME_$GET_ENTRY - Get directory entry by index
 * Original address: 0x00e4ad18
 */
void REM_NAME_$GET_ENTRY(void *param_1, void *param_2, uid_t *dir_uid,
                         int16_t index, void *entry_ret, status_$t *status_ret);

/*
 * REM_NAME_$FIND_NETWORK - Find network by name
 * Original address: 0x00e4add6
 */
void REM_NAME_$FIND_NETWORK(void *param_1, void *param_2, char *name,
                            uint16_t name_len, void *net_ret,
                            status_$t *status_ret);

/*
 * REM_NAME_$FIND_UID - Find object by UID
 * Original address: 0x00e4ae84
 */
void REM_NAME_$FIND_UID(void *param_1, void *param_2, uid_t *uid,
                        void *result_ret, status_$t *status_ret);

#endif /* NAME_H */
