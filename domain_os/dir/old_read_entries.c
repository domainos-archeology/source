/*
 * dir_$old_read_entries - Read directory entries into a buffer
 *
 * Internal implementation called by DIR_$OLD_DIR_READU.
 * Iterates through directory entries (both inline and overflow chain),
 * unmaps case on names via UNMAP_CASE, and copies entry data
 * (type byte, UID, and name) into the caller's buffer.
 *
 * The function maintains a cursor (param_2) tracking the current
 * position in the directory (slot index + overflow chain position).
 * It reads up to param_3 entries or until the buffer (param_4 bytes)
 * is exhausted.
 *
 * Entry output format (variable size, 4-byte aligned):
 *   offset 0x00: total entry size (uint16_t)
 *   offset 0x02: entry type byte
 *   offset 0x04: UID high (uint32_t)
 *   offset 0x08: UID low (uint32_t)
 *   offset 0x0C: create time or location (uint32_t)
 *   offset 0x10: cursor position (uint32_t)
 *   offset 0x14: name length (uint16_t)
 *   offset 0x16: name data (variable, null-terminated)
 *
 * Parameters:
 *   uid        - UID of directory
 *   param_2    - Cursor (uint16_t[2]: [0]=overflow_flag, [1]=slot_index)
 *   param_3    - Maximum number of entries to read
 *   param_4    - Buffer size in bytes
 *   param_5    - Output buffer pointer
 *   param_6    - Output: number of entries read
 *   status_ret - Output: status code
 *
 * Original address: 0x00E579C0
 * Size: 704 bytes
 *
 * TODO: Full implementation requires UNMAP_CASE helper and careful
 * handling of inline vs overflow entry layout differences.
 * The assembly has been verified against Ghidra output.
 */

#include "dir/dir_internal.h"

/* Stub - complex 704-byte function with inline/overflow iteration */
