/*
 * rem_name/rem_name.h - Remote Naming Service Functions
 *
 * Public header of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40): the entry points other subsystems call.  The
 * module's internal types and helpers are in rem_name/rem_name_internal.h.
 */

#ifndef REM_NAME_H
#define REM_NAME_H

#include "name/name.h"

/*
 * The REM_NAME_$* entry points of the REM_NAME module.  The declarations
 * moved here from name/name.h (bead source-3uo); the bodies moved from
 * name/rem_name.c to rem_name/, one file per function (bead source-ev4k).
 *
 * REM_NAME_$REGISTER_SERVER (0xE4A4AE) takes no parameters; its callers in
 * rip/server.c push two ignored arguments.
 *
 * There are two categories:
 * 1. Low-level functions that take explicit net/node parameters
 * 2. High-level wrappers that auto-locate a server and retry on failure
 */

/*
 * REM_NAME_SERVER_LOCAL - Check if naming server is on local node
 *
 * The one REM_NAME entry point whose name carries no `$'.
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

#endif /* REM_NAME_H */
