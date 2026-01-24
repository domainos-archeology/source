/*
 * DIR_$DROP_HARD_LINKU - Drop a hard link
 *
 * Removes a hard link from a directory.
 *
 * Original address: 0x00E516FC
 * Original size: 250 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DROP_HARD_LINKU - Drop a hard link
 *
 * Removes a hard link entry from a directory. The target file's
 * reference count is decremented. If the operation succeeds and
 * returns a UID that needs flushing, calls AST_$COND_FLUSH.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of link to drop
 *   name_len   - Pointer to name length
 *   flags      - Pointer to flags
 *   status_ret - Output: status code
 */
void DIR_$DROP_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                          uint16_t *flags, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint16_t  path_len;     /* Name length */
        uint16_t  drop_flags;   /* Flags for drop operation */
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
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Copy name into request buffer */
    request.path_len = len;
    for (i = 0; i < len; i++) {
        request.name_data[i] = name[i];
    }

    /* Build the request */
    request.op = DIR_OP_DROP_HARD_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fc52;
    request.drop_flags = *flags;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc56, 0x1c,
               (Dir_$OpResponse *)response.flags, &request);

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
        DIR_$OLD_DROP_HARD_LINKU(dir_uid, name, name_len, flags, status_ret);
    }
}
