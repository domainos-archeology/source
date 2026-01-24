/*
 * DIR_$CNAMEU - Change name (rename) an entry
 *
 * Renames a directory entry from one name to another.
 *
 * Original address: 0x00E51B68
 * Original size: 258 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$CNAMEU - Change name (rename) an entry
 *
 * Renames a directory entry within the same directory. Both names
 * must be valid leaf names (max 255 characters).
 *
 * Parameters:
 *   dir_uid      - UID of directory containing entry
 *   old_name     - Current name
 *   old_name_len - Pointer to current name length
 *   new_name     - New name
 *   new_name_len - Pointer to new name length
 *   status_ret   - Output: status code
 */
void DIR_$CNAMEU(uid_t *dir_uid, char *old_name, uint16_t *old_name_len,
                 char *new_name, uint16_t *new_name_len, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint16_t  old_len;      /* Old name length */
        uint16_t  new_len;      /* New name length */
        char      name_data[512]; /* Both names stored here */
    } request;
    Dir_$OpResponse response;
    status_$t status;
    uint16_t olen, nlen;
    int16_t i;

    /* Get lengths */
    olen = *old_name_len;
    nlen = *new_name_len;

    /* Validate both name lengths */
    if (olen == 0 || nlen == 0 || olen > DIR_MAX_LEAF_LEN || nlen > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Copy old name into request buffer */
    request.old_len = olen;
    for (i = 0; i < olen; i++) {
        request.name_data[i] = old_name[i];
    }

    /* Copy new name after old name */
    request.new_len = nlen;
    for (i = 0; i < nlen; i++) {
        request.name_data[olen + DAT_00e7fc66 + i] = new_name[i];
    }

    /* Build the request */
    request.op = DIR_OP_CNAMEU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fc62;

    /* Send the request - size includes both name lengths */
    DIR_$DO_OP(&request.op, olen + nlen + DAT_00e7fc66, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_CNAMEU(dir_uid, old_name, old_name_len, new_name, new_name_len, status_ret);
    } else {
        *status_ret = status;
    }
}
