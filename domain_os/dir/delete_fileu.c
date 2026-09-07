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
 *                      becomes request.flags1 (`movea.l D3,A0 /
 *                      move.b (A0),(-0x128,A6)` at 0x00E51626)
 *   no_lock          - POINTER to a Domain boolean (A6+0x1C); its byte
 *                      becomes request.flags2 (`movea.l D4,A1 /
 *                      move.b (A1),(-0x127,A6)` at 0x00E5162C)
 */
void DIR_$DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       status_$t *status_ret, boolean *check_del_right,
                       boolean *no_lock)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint16_t  path_len;     /* Name length */
        uint8_t   flags1;       /* the byte at *check_del_right */
        uint8_t   flags2;       /* the byte at *no_lock */
        char      name_data[255];
    } request;
    struct {
        uint8_t   flags[20];
        uid_t     flush_uid;
    } response;
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
    request.path_len = len;
    for (i = 0; i < len; i++) {
        request.name_data[i] = name[i];
    }

    /* Build the request */
    request.op = DIR_OP_DELETE_FILEU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fc72;
    /* 0x00E51628 / 0x00E5162E: both are single-byte reads through the two
     * pointers, not longword reads. */
    request.flags1 = (uint8_t)*check_del_right;
    request.flags2 = (uint8_t)*no_lock;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc76, 0x1c,
               (Dir_$OpResponse *)response.flags, &do_op_rcvd_len);

    /* Store status from response */
    status = *((status_$t *)&response.flags[4]);
    *status_ret = status;

    /* Check if we need to flush and have a valid UID */
    if ((response.flags[0x13] & 1) != 0 && status == status_$ok) {
        /* Check if flush_uid is not NIL */
        if (response.flush_uid.high != UID_$NIL.high ||
            response.flush_uid.low != UID_$NIL.low) {
            /* Perform conditional flush */
            flush_flags = 0;
            AST_$COND_FLUSH(&response.flush_uid, &flush_flags, &flush_status);
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
