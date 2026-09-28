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
 * REM_NAME_$REGISTER_SERVER (0xE4A4AE) is passed two pointers that its body
 * never reads; see its declaration below.
 *
 * There are two categories:
 * 1. Low-level functions that take explicit net/node parameters
 * 2. High-level wrappers that auto-locate a server and retry on failure
 */

/*
 * rem_name_$dir_entry_t - the 0x30-byte directory entry every REM_NAME lookup
 * hands back (entry_ret / entries_ret).  Offsets from the stores in
 * REM_NAME_$GET_ENTRY_BY_NAME (0x00E4A622-0x00E4A676) and REM_NAME_$READ_DIR
 * (0x00E4AA74-0x00E4AB12; there the record is addressed from its END,
 * `lea (0x0,A4,D0*0x1),A3` with D0 = count*0x30, so name_len is (-0x2e,A3)).
 *   type      1 = ordinary entry (uid + extra filled), 3 = link (uid NIL,
 *             extra 0 in the GET_ENTRY_BY_NAME case; left as found by
 *             READ_DIR), 0 = not found
 */
typedef struct rem_name_$dir_entry_t {
    int16_t     type;           /* 0x00 */
    uint16_t    name_len;       /* 0x02 */
    char        name[32];       /* 0x04: READ_DIR pads with spaces to 32 */
    uid_t       uid;            /* 0x24 */
    uint32_t    extra;          /* 0x2C */
} rem_name_$dir_entry_t;

_Static_assert(__builtin_offsetof(rem_name_$dir_entry_t, name)  == 0x04, "rem_name_$dir_entry_t.name");
_Static_assert(__builtin_offsetof(rem_name_$dir_entry_t, uid)   == 0x24, "rem_name_$dir_entry_t.uid");
_Static_assert(__builtin_offsetof(rem_name_$dir_entry_t, extra) == 0x2C, "rem_name_$dir_entry_t.extra");
_Static_assert(sizeof(rem_name_$dir_entry_t) == 0x30, "sizeof rem_name_$dir_entry_t");

/*
 * rem_name_$rep_entry_t - one 0x12-byte replica record REM_NAME_$READ_REP
 * copies out (four longwords and a word, 0x00E4AC06-0x00E4AC0E).
 */
typedef struct __attribute__((packed, aligned(2))) rem_name_$rep_entry_t {
    uint32_t    words[4];       /* 0x00 */
    uint16_t    tail;           /* 0x10 */
} rem_name_$rep_entry_t;

_Static_assert(sizeof(rem_name_$rep_entry_t) == 0x12, "sizeof rem_name_$rep_entry_t");

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
 * The body (0x00E4A4AE..0x00E4A4C6, 26 bytes) is only
 *
 *   lea (0xe7dbb8).l,A0                     ; the REM_NAME data area
 *   move.l (0x00e2b0d4).l,(0x28,A0)         ; record +0x28 <- TIME_$CLOCKH
 *   st (0x3c,A0)                            ; record +0x3C <- true
 *
 * so neither argument is read - but every call site pushes both, so the
 * prototype carries them.  The arguments identify the server that was heard
 * from, as a (network, node) pair passed by reference:
 *
 *   0x00E69152/0x00E69158 RIP_$ANNOUNCE_NS pushes #0xE245A4 (NODE_$ME) then
 *     #0xE2E0A0 (ROUTE_$PORT, aliased NETWORK_$ME in the SAU2 map).  The last
 *     push is argument 1, so the call is (&ROUTE_$PORT, &NODE_$ME).
 *   0x00E68DF2/0x00E68DF6 RIP_$PROCESS_REQUEST's ring path pushes
 *     (&reg_network, &reg_node_id).
 *   0x00E68E04/0x00E68E08 its internet path pushes (&src_node_or, &src_node).
 *
 * Original address: 0x00e4a4ae
 */
void REM_NAME_$REGISTER_SERVER(uint32_t *net, uint32_t *node);

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
 *   start_index - Index of first entry to read (a WORD: `move.w (0x14,A6)`
 *                 at 0x00E4A992, zero-extended into the request)
 *   entries_ret - Output: array of directory entries (0x30 bytes each)
 *   max_entries - Maximum entries to return
 *   count_ret   - Output: actual number of entries returned
 *   status_ret  - Output: status code
 *
 * Original address: 0x00e4a984
 */
void REM_NAME_$READ_DIR(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint16_t start_index, void *entries_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret);

/*
 * REM_NAME_$READ_REP - Read replication information (low-level)
 *
 * Parameters:
 *   net         - Network ID
 *   node        - Node ID of naming server
 *   dir_uid     - UID of directory
 *   start_index - Index of first replica entry (a WORD: `move.w (0x14,A6)`
 *                 at 0x00E4AB52, zero-extended into the request)
 *   rep_ret     - Output: array of replica entries (0x12 bytes each)
 *   max_entries - Maximum entries to return
 *   count_ret   - Output: actual number of entries returned
 *   status_ret  - Output: status code
 *
 * Original address: 0x00e4ab44
 */
void REM_NAME_$READ_REP(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint16_t start_index, void *rep_ret,
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
