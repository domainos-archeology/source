/*
 * FILE_$FW_FILE - Force Write File
 *
 * Original address: 0x00E5E622
 * Size: 94 bytes
 *
 * Forces all dirty pages of a file to be written back to disk.
 * This is a "force write" operation that ensures file data durability.
 *
 * Operation:
 * 1. Calls FILE_$DELETE_INT with flags=0 to check if file is locked
 *    (does not actually delete, just checks lock status)
 * 2. Selects purify flags based on lock status:
 *    - If locked (DELETE_INT returns negative): flags = 2 (minimal purify)
 *    - If not locked: flags = 0x8002 (full purify with remote sync)
 * 3. Calls AST_$PURIFY to flush dirty pages to disk
 *
 * The flags passed to AST_$PURIFY:
 *   bit 1 (0x2): Update segment timestamp
 *   bit 15 (0x8000): Remote flag - sync to remote storage if applicable
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "ast/ast.h"

/* Purify flags */
#define FW_PURIFY_LOCAL_ONLY   0x0002   /* Local purify only */
#define FW_PURIFY_WITH_REMOTE  0x8002   /* Include remote sync */

void FILE_$FW_FILE(uid_t *file_uid, status_$t *status_ret)
{
    int8_t was_locked;
    uint8_t delete_result[2];  /* Result buffer from DELETE_INT */
    uint16_t purify_flags;

    /*
     * Check if file is locked by calling FILE_$DELETE_INT with flags=0.
     * This doesn't actually delete - it just queries lock status.
     * Returns negative if the file has any locks.
     */
    was_locked = FILE_$DELETE_INT(file_uid, 0, delete_result, status_ret);

    /*
     * Select purify flags based on lock status.
     * If locked, use minimal flags (local-only) to avoid blocking.
     * If not locked, use full flags including remote sync.
     */
    if (was_locked < 0) {
        purify_flags = FW_PURIFY_LOCAL_ONLY;
    } else {
        purify_flags = FW_PURIFY_WITH_REMOTE;
    }

    /*
     * Call AST_$PURIFY to flush dirty pages.
     * Parameters:
     *   uid        - file UID
     *   flags      - purify operation flags
     *   segment    - 0 (all segments)
     *   page_list  - NULL (not used for whole-file purify)
     *   page_count - 0
     *   status_ret - output status
     */
    AST_$PURIFY(file_uid, purify_flags, 0, NULL, 0, status_ret);
}
