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
 * NAME_$INIT_FUN_00e31578 - Debug/logging helper for NAME_$INIT
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
 * FUN_00e4a060 - Internal pathname resolution helper
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
 * FUN_00e58488 - Map a directory for fast access
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
boolean FUN_00e58488(uid_t *dir_uid, int16_t flags, void *mapped_info, status_$t *status_ret);

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

#endif /* NAME_INTERNAL_H */
