/*
 * DIR_$ADD_LINKU - Add a soft/symbolic link
 *
 * Creates a symbolic link pointing to a pathname.
 *
 * Original address: 0x00E5068E
 * Original size: 258 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ADD_LINKU - Add a soft/symbolic link
 *
 * Creates a symbolic link entry in a directory that points to
 * another pathname (which may or may not exist).
 *
 * Parameters:
 *   dir_uid    - UID of directory for link
 *   name       - Name for the link
 *   name_len   - Pointer to name length (max 255)
 *   target     - Target pathname
 *   target_len - Pointer to target length (max 1023)
 *   status_ret - Output: status code
 */
void DIR_$ADD_LINKU(uid_t *dir_uid, char *name, int16_t *name_len,
                    void *target, uint16_t *target_len, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint16_t  path_len;     /* Name length */
        uint32_t  target_ptr;   /* Target pathname pointer */
        char      name_data[255 + 1024]; /* Name + target data */
    } request;
    Dir_$OpResponse response;
    status_$t status;
    uint16_t len, tlen;
    int16_t i;
    uint32_t total_size;

    /* Get lengths */
    len = (uint16_t)*name_len;
    tlen = *target_len;

    /* Validate name length */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Validate target length and total size */
    if (tlen == 0 || tlen > DIR_MAX_LINK_LEN) {
        *status_ret = status_$naming_invalid_link;
        return;
    }

    /* Check total packet size won't exceed limit */
    total_size = (uint32_t)DAT_00e7fc8e + (uint32_t)tlen + (uint32_t)len + 0x8e;
    if (total_size > 0x500) {
        *status_ret = status_$naming_invalid_link;
        return;
    }

    /* Copy name into request buffer */
    request.path_len = len;
    for (i = 0; i < len; i++) {
        request.name_data[i] = name[i];
    }

    /* Copy target into request buffer after name */
    /* Note: target starts at name_data + len */
    for (i = 0; i < (int16_t)tlen; i++) {
        request.name_data[len + i] = ((char *)target)[i];
    }

    /* Build the request */
    request.op = DIR_OP_ADD_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fc8a;

    /* Send the request - size includes name and target length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc8e, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == status_$file_bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_ADD_LINKU(dir_uid, name, name_len, target, target_len, status_ret);
    } else {
        *status_ret = status;
    }
}
