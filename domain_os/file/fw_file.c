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
    /*
     * A6-0x4, the cell handed to FILE_$DELETE_INT ("pea (-0x4,A6)" at
     * 0x00E5E63C).  purify_flags below is the image's word at A6-0x2, i.e.
     * the LOW half of that same four-byte frame slot: the flags word is
     * written into it at 0x00E5E650 / 0x00E5E658, after FILE_$DELETE_INT has
     * returned and its answer has been taken from D0, so nothing reads the
     * overwritten half and the two are kept as separate C objects.
     */
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
     * 0x00E5E65E-0x00E5E670: call AST_$PURIFY over a discarded word result
     * slot.  Arguments, right to left:
     *    pea (A3)                 status  = status_ret
     *    clr.w -(SP)              unused  = 0
     *    pea (-0x48,PC)           segment_list = &file_$nil_cell (0x00E5E61E)
     *    clr.w -(SP)              segment = 0
     *    move.w (-0x2,A6),-(SP)   flags   = purify_flags
     *    pea (A2)                 uid     = file_uid
     * The segment list is the shared zero longword, not nil; AST_$PURIFY does
     * not read through it for either flags value used here (neither 0x0002
     * nor 0x8002 sets bit 4, the gate at 0x00E05700).
     */
    AST_$PURIFY(file_uid, purify_flags, 0, &file_$nil_cell, 0, status_ret);
}
