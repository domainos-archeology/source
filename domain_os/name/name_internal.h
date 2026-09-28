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

/* Crash message string at 0x00E5855C used by name_$map_dir */
/* 0x00e5855c: a status_$t constant cell (0x000E0025) that NAME_$MAP_DIR
 * pea's to CRASH_SYSTEM at 0x00e5852a, not a string. */
extern status_$t Naming_Internal_Err;

/* Directory handles: NAME_$HANDLE_TO_PTR / NAME_$PTR_TO_HANDLE live in
 * name/name.h - dir/ uses them too. */

/*
 * name_$init_check_status (0x00e31578) - Status check / crash helper for
 * NAME_$INIT.  Not declared here: it is a nested Pascal subprocedure of
 * NAME_$INIT and is emitted as a static function in name/init.c.
 */

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

/*
 * name_$old_add_link_local (0x00E565B8, was FUN_00e565b8)
 *
 * The local half of name_$old_add_link, its only caller (0x00E56910).
 * Argument order from that call's pushes (0x00E56900-0x00E5690E, right to
 * left: status, file_uid, name_len, name, a constant zero word, dir_uid); the
 * callee frame confirms it - A6+0x08 long, +0x0C word, +0x0E long, +0x12 word,
 * +0x14 long, +0x18 long (0x00E565C0-0x00E565CC).
 *
 * The word at A6+0x0C is NAME_$LOCK_DIR's `acl_rights` argument
 * (`move.w (0xc,A6),-(SP)` right below `move.w #0x4` at 0x00E56620-0x00E56628)
 * and name_$old_add_entry's `type` on the root-directory path (0x00E565EE);
 * name_$old_add_link passes 0, which is what makes NAME_$LOCK_DIR skip its own
 * ACL_$RIGHTS check.
 *
 * Emitted in name/old_add_link_local.c.
 */
void name_$old_add_link_local(uid_t *dir_uid, int16_t acl_rights, char *name,
                              uint16_t name_len, uid_t *file_uid,
                              status_$t *status_ret);

#endif /* NAME_INTERNAL_H */
