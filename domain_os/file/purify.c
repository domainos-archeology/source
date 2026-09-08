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
 * Flow (0x00E5E5FE-0x00E5E610), the six arguments pushed right to left over a
 * discarded word result slot:
 *    subq.l #0x2,SP           the word AST_$PURIFY returns; discarded
 *    move.l (0xc,A6),-(SP)    status  = status_ret
 *    clr.w -(SP)              unused  = 0
 *    pea (0x16,PC)            segment_list = &file_$nil_cell (0x00E5E61E)
 *    clr.l -(SP)              flags = 0 and segment = 0, cleared together
 *    move.l (0x8,A6),-(SP)    uid     = file_uid
 */
void FILE_$PURIFY(uid_t *file_uid, status_$t *status_ret)
{
    /*
     * The segment-list argument is NOT nil: it is the address of the shared
     * longword-of-zeroes at 0x00E5E61E (see file/file_data.c).  AST_$PURIFY
     * does not read through it unless bit 4 of its flags word is set
     * (0x00E05700), which this call does not do.
     */
    AST_$PURIFY(file_uid, 0, 0, &file_$nil_cell, 0, status_ret);
}
