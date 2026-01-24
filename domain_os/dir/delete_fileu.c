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
 *   param4     - Status output parameter
 *   param5     - Flags parameter
 *   status_ret - Output: status code
 */
void DIR_$DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       status_$t *param4, void *param5, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint16_t  path_len;     /* Name length */
        uint8_t   flags1;       /* First flags byte from param5 */
        uint8_t   flags2;       /* Second flags byte from status_ret */
        char      name_data[255];
    } request;
    struct {
        uint8_t   flags[20];
        uid_t     flush_uid;
    } response;
    status_$t status;
    uint32_t flush_flags;
    status_$t flush_status;
    uint16_t len;
    int16_t i;

    /* Get name length */
    len = *name_len;

    /* Validate name length */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *param4 = status_$naming_invalid_leaf;
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
    request.flags1 = *((uint8_t *)param5);
    request.flags2 = *((uint8_t *)status_ret);

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc76, 0x1c,
               (Dir_$OpResponse *)response.flags, &request);

    /* Store status from response */
    status = *((status_$t *)&response.flags[4]);
    *param4 = status;

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
        DIR_$OLD_DELETE_FILEU(dir_uid, name, name_len, param4, param5, status_ret);
    }
}
