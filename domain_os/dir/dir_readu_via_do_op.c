/*
 * dir_$dir_readu_via_do_op - Read directory entries via DIR_$DO_OP
 *
 * Originally a nested Pascal subprocedure of DIR_$DIR_READU. Reads
 * directory entries by issuing DIR_$DO_OP requests (opcode 0x42) in
 * a loop until the output buffer is full, max_entries is reached,
 * continuation becomes 0 (EOF), or a size limit (0x400) is reached.
 *
 * Each DO_OP call returns a batch of entries. The last entry in each
 * batch provides the continuation name for the next call. The function
 * copies the continuation name between calls and accumulates entries
 * into the caller's output buffer.
 *
 * Access pattern (Pascal frame chain via A6):
 *   parent+0x08 = dir_uid pointer
 *   parent+0x0C = name buffer pointer
 *   parent+0x10 = name_len pointer
 *   parent+0x14 = continuation pointer
 *   parent+0x18 = max_entries pointer
 *   parent+0x1C = buffer_size pointer
 *   parent+0x20 = output buffer base
 *   parent+0x24 = count_ret pointer
 *
 * Parameters:
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4E1FE
 * Original size: 424 bytes
 *
 * TODO(source-qgq): This function accesses parent stack frames (Pascal nested proc).
 * The A5-relative data at offsets 0x20A2 and 0x20A6 are protocol version
 * and request size parameters for DIR_OP_DIR_READU. The loop termination
 * conditions and buffer management need verification.
 */

#include "dir/dir_internal.h"

/* A5-relative data for DIR_READU protocol parameters */
/* DAT_A5_20A2: protocol version for DIR_READU */
/* DAT_A5_20A6: base request size for DIR_READU */

void dir_$dir_readu_via_do_op(status_$t *status_ret)
{
    /* TODO(source-qgq): This function is a nested Pascal subprocedure that accesses
     * its parent's stack frame (via A6 chain) to reach all the
     * DIR_$DIR_READU parameters. The Ghidra decompilation shows
     * unaff_A6-relative accesses for all parameters.
     *
     * High-level logic:
     *
     * 1. Build DO_OP request:
     *    - Set opcode = 0x42
     *    - Copy dir_uid from parent frame
     *    - Set protocol version from A5+0x20A2
     *    - Clear count_ret
     *
     * 2. Check name_len < 256 (else return naming_invalid_leaf)
     *
     * 3. Copy starting name into request buffer (up to 255 chars)
     *
     * 4. Loop:
     *    a. Set continuation, max_entries, buffer_size in request
     *    b. Call DIR_$DO_OP(request, name_len + req_base_size, 0x24,
     *                       response, received_len).  The fifth argument is
     *                       `pea (-0x1d6,A6)` at 0x00E4E292 against the
     *                       request at `pea (-0x1c8,A6)` (0x00E4E2A8): a
     *                       2-byte reply-length cell, not the request
     *                       (source-32ld).
     *    c. Copy status from response
     *    d. On success:
     *       - Update continuation from response
     *       - If entry count > 0:
     *         * Find last entry, extract continuation name
     *         * Validate name_len < 256 (crash if not)
     *         * Copy continuation name to request buffer
     *         * Subtract entry data size from remaining buffer
     *         * Crash if buffer underflow
     *         * Advance output pointer by entry data size
     *       - Continue if:
     *         * continuation != 0 AND
     *         * count_ret < max_entries AND
     *         * NOT (buffer_size < 0x400 OR remaining == 0)
     *
     * 5. On completion:
     *    - If continuation == 0: clear name_len (EOF marker)
     *    - Else if count_ret > 0: copy last continuation name back
     *      to parent's name buffer
     */

    /* Assembly-faithful implementation would require accessing the
     * caller's frame pointer chain, which is inherently M68K-specific.
     * This function should be regenerated as inline code within
     * DIR_$DIR_READU if retargetability is needed. */
}
