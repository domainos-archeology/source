/*
 * DIR_$READ_LINKU - Read a symbolic link
 *
 * Reads the target pathname of a symbolic link.
 *
 * Original address: 0x00E4D6C0
 * Original size: 240 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$READ_LINKU - Read a symbolic link
 *
 * Reads the target pathname of a symbolic link entry in a directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory containing link
 *   name       - Name of link
 *   name_len   - Pointer to name length
 *   buf_len    - Pointer to buffer length
 *   target     - Target pathname buffer
 *   target_len - Output: actual target length
 *   target_uid - Output: target UID
 *   status_ret - Output: status code
 */
void DIR_$READ_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     int16_t *buf_len, void *target, uint16_t *target_len,
                     uid_t *target_uid, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint16_t  link_buf_len; /* Buffer length for link target */
        uint16_t  path_len;     /* Name length */
        void     *target_ptr;   /* Target pathname pointer */
        char      name_data[255];
    } request;
    Dir_$OpResponse response;
    status_$t status;
    uint16_t len, tlen;
    int16_t i;

    /* Get name length */
    len = *name_len;

    /* Validate name length */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Validate buffer length */
    tlen = (uint16_t)*buf_len;
    if (tlen < 1) {
        *status_ret = status_$naming_object_is_not_an_acl_object;
        return;
    }

    /* Copy name into request buffer */
    request.path_len = len;
    for (i = 0; i < len; i++) {
        request.name_data[i] = name[i];
    }

    /* Build the request */
    request.op = DIR_OP_READ_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fc92;
    request.link_buf_len = tlen;
    request.target_ptr = target;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc96, 0x1e, &response, &request);

    /* Store results */
    *status_ret = response.status;
    target_uid->high = response._22_4_;
    target_uid->low = response.f1a;
    *target_len = response._20_2_;

    /* Check for fallback conditions */
    if (*status_ret == file_$bad_reply_received_from_remote_node ||
        *status_ret == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_READ_LINKU((int16_t)(uintptr_t)dir_uid,
                            (int16_t)(uintptr_t)name, name_len,
                            (int16_t)(uintptr_t)target, (int16_t *)target_len,
                            target_uid, status_ret);
    }
}
