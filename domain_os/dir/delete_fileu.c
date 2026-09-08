/*
 * DIR_$DELETE_FILEU - Delete a file from directory
 *
 * Removes a file entry and optionally deletes the file.
 *
 * Original address: 0x00E515BC
 * Original size: 262 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DELETE_FILEU - Delete a file from directory
 *
 * Deletes a file entry from a directory. If the operation succeeds
 * and returns a UID that needs flushing, calls AST_$COND_FLUSH.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of entry to delete
 *   name_len   - Pointer to name length (max 255)
 *   status_ret       - Output: status code (A6+0x14; `move.l #0xe000b,(A4)`
 *                      at 0x00E515EC and `move.l (-0x1c,A6),(A4)` at
 *                      0x00E51654)
 *   check_del_right  - POINTER to a Domain boolean (A6+0x18); its byte
 *                      becomes body.delete_file.flag0 (`movea.l D3,A0 /
 *                      move.b (A0),(-0x128,A6)` at 0x00E51626)
 *   no_lock          - POINTER to a Domain boolean (A6+0x1C); its byte
 *                      becomes body.delete_file.flag1 (`movea.l D4,A1 /
 *                      move.b (A1),(-0x127,A6)` at 0x00E5162C)
 */
void DIR_$DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       status_$t *status_ret, boolean *check_del_right,
                       boolean *no_lock)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-relative 2-byte cell passed as DIR_$DO_OP's fifth argument;
     * it is REM_FILE_$SEND_REQUEST's `received_len` out-parameter
     * (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint32_t flush_flags;
    status_$t flush_status;
    uint16_t len;
    int16_t i;

    /* Get name length */
    len = *name_len;

    /* Validate name length */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Copy name into request buffer */
    request.body.delete_file.path_len = len;
    for (i = 0; i < len; i++) {
        request.body.delete_file.name[i] = name[i];
    }

    /* Build the request */
    request.op = DIR_OP_DELETE_FILEU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_DELETE_FILEU >> 1).version;
    /* 0x00E51628 / 0x00E5162E: both are single-byte reads through the two
     * pointers, not longword reads. */
    /* 0x00E51626 / 0x00E5162E: the two boolean BYTES at +0x90 and +0x91. */
    request.body.delete_file.flag0 = (uint8_t)*check_del_right;
    request.body.delete_file.flag1 = (uint8_t)*no_lock;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_DELETE_FILEU >> 1).base_size + len),
               0x1c, &response, &do_op_rcvd_len);

    /* Store status from response */
    status = response.status;
    *status_ret = status;

    /* Check if we need to flush and have a valid UID */
    if ((response.f18[DIR_RESP_REMOTE_FLAG_BYTE] & DIR_RESP_REMOTE_FLAG) != 0 &&
        status == status_$ok) {
        /* Check if flush_uid is not NIL */
        if (response.uid.high != UID_$NIL.high ||
            response.uid.low != UID_$NIL.low) {
            /* Perform conditional flush */
            flush_flags = 0;
            AST_$COND_FLUSH(&response.uid, &flush_flags, &flush_status);
            return;
        }
    }

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        /* 0x00E516A6-0x00E516B2: a straight pass-through of all six
         * parameters, in order. */
        DIR_$OLD_DELETE_FILEU(dir_uid, name, name_len, status_ret,
                              check_del_right, no_lock);
    }
}
