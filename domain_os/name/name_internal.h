/*
 * NAME - Internal Header
 *
 * Internal types, data structures, and helper functions for the NAME subsystem.
 * This file should only be included by .c files within the name/ directory.
 */

#ifndef NAME_INTERNAL_H
#define NAME_INTERNAL_H

#include "acl/acl.h"
#include "dir/dir.h"
#include "file/file.h"
#include "misc/crash_system.h"
#include "misc/string.h"
#include "name/name.h"
#include "vtoc/vtoc.h"
#include "proc1/proc1.h"
#include "vfmt/vfmt.h"
#include "cal/cal.h"
#include "network/network.h"
#include "time/time.h"
#include "mst/mst.h"
#include "ast/ast.h"
#include "os/os.h"
#include "ec/ec.h"
#include "pkt/pkt.h"
#include "sock/sock.h"

/*
 * NAME data area
 *
 * The name subsystem uses a data area at 0xE80264 which contains:
 *   - Per-ASID directory state (working dir, naming dir, etc.)
 *   - Global UIDs for system directories
 *   - Mapped info structures for directory caching
 *
 * Layout at 0xE80264:
 *   +0x00: NAME_$NODE_DATA_UID (8 bytes)
 *   +0x08: NAME_$COM_MAPPED_INFO (16 bytes)
 *   +0x18: NAME_$COM_UID (8 bytes)
 *   +0x20: NAME_$NODE_MAPPED_INFO (16 bytes)
 *   +0x30: NAME_$NODE_UID (8 bytes)
 *   +0x38: NAME_$ROOT_UID (8 bytes)
 *
 * Additional data at 0xE35040:
 *   - name_$data: 256-byte path buffer used during initialization
 */

/* Internal path strings for validation - defined in validate.c */

/*
 * Constants in the NAME code region passed by reference (Pascal VAR args)
 */
extern int16_t DAT_00e544ae;    /* 0xE544AE: 0x0020 - MAP_CASE max output length (32) */

/* Crash message string at 0x00E5855C used by name_$map_dir */
extern char Naming_Internal_Err[];

/*
 * REM_NAME data area - complete structure at 0xE7DBB8
 */
typedef struct rem_name_data_t {
    uint16_t config[15];             /* +0x00: Config copied to request packets */
    uint16_t reserved1;              /* +0x1E: Reserved */
    uint32_t server_timeout;         /* +0x20: Timeout for server contact */
    uint32_t reserved2;              /* +0x24: Reserved */
    uint32_t time_heard_from_server; /* +0x28: TIME_$CLOCKH when last heard */
    status_$t last_status;           /* +0x2C: Last status code */
    uint32_t curr_node;              /* +0x30: Current name server node */
    uint32_t curr_net;               /* +0x34: Current name server network */
    uint16_t pkt_seq_num;            /* +0x38: Packet sequence number */
    uint16_t retry_count;            /* +0x3A: Server locate retry counter */
    int8_t   heard_from_server;      /* +0x3C: True if contacted server */
} rem_name_data_t;

extern rem_name_data_t rem_name_$data;  /* 0xE7DBB8 - defined in rem_name.c */

/*
 * Socket used by the remote naming service.
 *
 * REM_NAME_SERVER_LOCAL (0x00E4A408) does
 *     movea.l (0x00e28dd8).l,A0 ; move.w (0x16,A0),D0w ; btst.l #0xd,D0
 * 0xE28DD8 is not an eventcount of its own: it is slot 10 of the SOCK socket
 * pointer table (sock_table_base + 0x18A4 + 9*4, that is
 * SOCK_$EVENT_COUNTERS[REM_NAME_$SOCK - 1]), and the word at +0x16 of the
 * socket descriptor it points at is sock_$sock_t.flags.  Bit 13 of that word
 * means "the name server runs on this node".  The declaration therefore lives
 * in sock/sock.h; only the socket number belongs to NAME.
 */
/* Directory handles: NAME_$HANDLE_TO_PTR / NAME_$PTR_TO_HANDLE live in
 * name/name.h - dir/ uses them too. */

#define REM_NAME_$SOCK          10      /* well-known naming-service socket */
#define SOCK_FLAG_SERVER_LOCAL  0x2000  /* sock_$sock_t.flags bit 13 */

/*
 * NAME_$INIT_FUN_00e31578 - Debug/logging helper for NAME_$INIT
 *   (Ghidra name: name_$init_check_status at 0x00e31578)
 *
 * Called during NAME_$INIT to log progress. Parameters suggest it takes
 * a format string and optional data.
 *
 * Parameters:
 *   msg     - Message/format string
 *   param1  - First parameter (often a pointer to data)
 *   param2  - Second parameter (often a length or flags)
 *
 * Original address: 0x00e31578
 */
void NAME_$INIT_FUN_00e31578(char *msg, void *param1, int param2);

/*
 * name_$resolve_internal (0x00e4a060; declared below as FUN_00e4a060) - Internal pathname resolution helper
 *
 * Called by NAME_$RESOLVE to do the actual resolution work.
 * Returns both directory UID and file UID.
 *
 * Parameters:
 *   path       - Pathname to resolve
 *   path_len   - Length of pathname (value, not pointer)
 *   dir_uid    - Output: UID of containing directory
 *   file_uid   - Output: UID of the named object
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a060
 */
void FUN_00e4a060(char *path, int16_t path_len, uid_t *dir_uid, uid_t *file_uid,
                  status_$t *status_ret);

/*
 * name_$map_dir - Map a directory for fast access
 *
 * Sets up mapped info structure for a directory.
 *
 * Parameters:
 *   dir_uid     - UID of directory to map
 *   flags       - Mapping flags
 *   mapped_info - Output: mapped info structure
 *   status_ret  - Output: status code
 *
 * Returns:
 *   0xFF on success, 0 or positive on failure
 *
 * Original address: 0x00e58488
 */
boolean name_$map_dir(uid_t *dir_uid, int16_t asid,
                      name_$mapped_info_t *mapped_info, status_$t *status_ret);

/*
 * name_$split_path - Split path into directory and filename portions
 *
 * Scans backwards from end of path to find the last '/'.
 * Returns the directory length (without trailing slash, except for root)
 * and the filename position and length.
 *
 * Parameters:
 *   path             - The full pathname
 *   path_len         - Length of pathname (1-indexed)
 *   dirname_len_ret  - Output: length of directory portion
 *   filename_idx_ret - Output: 1-indexed position of filename start
 *   filename_len_ret - Output: length of filename
 *
 * Original address: 0x00e49e48
 */
void name_$split_path(char *path, uint16_t path_len, uint16_t *dirname_len_ret,
                      uint16_t *filename_idx_ret, int16_t *filename_len_ret);

/*
 * name_$resolve_internal - Internal pathname resolution
 *
 * Called by NAME_$RESOLVE to perform the actual resolution.
 * Handles different path types and traverses directory entries.
 *
 * Parameters:
 *   path         - The pathname to resolve
 *   path_len     - Length of pathname (value, not pointer)
 *   dir_uid_ret  - Output: UID of containing directory
 *   file_uid_ret - Output: UID of the named object
 *   status_ret   - Output: status code
 *
 * Original address: 0x00e4a060
 */
void name_$resolve_internal(char *path, int16_t path_len, uid_t *dir_uid_ret,
                            uid_t *file_uid_ret, status_$t *status_ret);

/*
 * name_$resolve_dir_and_leaf - Resolve directory and get leaf info
 *
 * Splits a path into directory and filename, then resolves the directory
 * to its UID.
 *
 * Parameters:
 *   path             - The full pathname
 *   path_len         - Length of pathname
 *   filename_idx_ret - Output: 1-indexed position of filename
 *   filename_len_ret - Output: length of filename
 *   dir_uid_ret      - Output: UID of the parent directory
 *   status_ret       - Output: status code
 *
 * Returns:
 *   true (0xFF) if directory was found, false (0) otherwise
 *
 * Original address: 0x00e4a1e2
 */
boolean name_$resolve_dir_and_leaf(char *path, int16_t path_len,
                                   uint16_t *filename_idx_ret, int16_t *filename_len_ret,
                                   uid_t *dir_uid_ret, status_$t *status_ret);

/*
 * name_$unmap_dir_buffers - Unmap directory memory-mapped buffers
 *
 * Unmaps the memory regions used by a directory's mapped info structure.
 * If the two buffer halves are contiguous (base + 0x8000 == second_base),
 * unmaps a single 0x10000 region; otherwise unmaps two 0x8000 regions.
 * Clears the active flag in the mapped info structure.
 *
 * Parameters:
 *   asid        - Address space ID for the unmap operation
 *   mapped_info - Pointer to the directory mapped info structure
 *
 * Original address: 0x00E58560
 */
void name_$unmap_dir_buffers(int16_t asid, name_$mapped_info_t *mapped_info);

#endif /* NAME_INTERNAL_H */
