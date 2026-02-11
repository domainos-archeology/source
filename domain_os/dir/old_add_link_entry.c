/*
 * dir_$old_add_link_entry - Add symbolic link entry to directory
 *
 * Allocates overflow blocks for the link target data and adds a
 * type 3 (soft link) entry to the directory. Process:
 *
 * 1. Allocate first overflow block via FUN_00e54e10 (free list)
 *    or FUN_00e54e62 (hash-based) if free list is empty
 * 2. Copy first 0x90 bytes of target data to block at offset 0x370
 *    (block_idx * 0x96 + 0x370), with type byte 3 at offset 0x36f
 * 3. If target_len > 0x90:
 *    a. Allocate second overflow block
 *    b. Copy remaining bytes (offset 0x90..target_len) to second block
 *    c. On allocation failure: free first block, return error
 * 4. Build link descriptor: {target_len, block1_idx, block2_idx, 0}
 * 5. Call dir_$old_add_entry with type=3 and link descriptor as uid_data
 * 6. On add failure: free allocated blocks via FUN_00e5518c
 * On allocation failure: returns status 0xE0002 (directory full)
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   handle     - Mapped directory buffer base
 *   name       - Entry name
 *   name_len   - Length of name
 *   target     - Link target text
 *   target_len - Length of target text
 *   flags      - Flags byte (passed through to dir_$old_add_entry)
 *   result     - Output: result buffer
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5545C
 * Size: 384 bytes
 *
 * TODO: Implement fully - requires FUN_00e54e10 (allocate overflow slot
 * from free list) and FUN_00e54e62 (allocate with hash hint).
 */

#include "dir/dir_internal.h"

/* Stub - 384-byte symbolic link entry addition */
