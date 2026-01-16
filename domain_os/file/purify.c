/*
 * FILE_$PURIFY - Purify (flush) a file's dirty pages
 *
 * Wrapper function that flushes dirty pages of a file to disk.
 * Calls AST_$PURIFY with flags=0 to perform a basic purification.
 *
 * Original address: 0x00E5E5F2
 */

#include "file/file_internal.h"

/*
 * FILE_$PURIFY
 *
 * Purifies a file by flushing its dirty pages to disk.
 * This is a simple wrapper around AST_$PURIFY.
 *
 * Parameters:
 *   file_uid   - UID of file to purify
 *   status_ret - Output status code
 *
 * Flow:
 * 1. Call AST_$PURIFY with:
 *    - uid = file_uid
 *    - flags = 0 (basic purify)
 *    - segment = 0 (all segments)
 *    - segment_list = NULL (empty constant)
 *    - unused = 0
 *    - status = status_ret
 */
void FILE_$PURIFY(uid_t *file_uid, status_$t *status_ret)
{
    /*
     * Call AST_$PURIFY with basic flags.
     * The segment_list parameter is NULL (no specific segments),
     * and the unused parameter is 0.
     */
    AST_$PURIFY(file_uid, 0, 0, NULL, 0, status_ret);
}
