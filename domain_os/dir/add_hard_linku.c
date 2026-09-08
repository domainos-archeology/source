/*
 * DIR_$ADD_HARD_LINKU - Add a hard link
 *
 * Original address: 0x00E505CA
 * Original size: 196 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ADD_HARD_LINKU (0x00E505CA)
 *
 * Builds a DIR_OP_ADD_HARD_LINKU request and sends it through DIR_$DO_OP,
 * falling back to DIR_$OLD_ADD_HARD_LINKU (0x00E5697A).
 *
 * Frame: `link.w A6,-0x1b8` - request base A6-0x1B0, reply A6-0x18
 * (0x14 bytes), received-length word A6-0x1B2.
 *
 * Parameters (A6+0x08..A6+0x18):
 *   dir_uid    - UID of the directory to hold the link
 *   name       - name for the link
 *   name_len   - pointer to the name length
 *   target_uid - UID of the file the link names
 *   status_ret - out: status code
 */
void DIR_$ADD_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1B2: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint16_t len;
    int16_t i;

    /* 0x00E505EC-0x00E505F4 */
    len = *name_len;
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E50600: length at request+0x8E. */
    request.body.uid_name.path_len = len;
    /* 0x00E50608-0x00E50616: name bytes at request+0x98. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.uid_name.name[i] = name[i];
    }

    /* 0x00E5061A-0x00E5062A */
    request.op = DIR_OP_ADD_HARD_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_ADD_HARD_LINKU >> 1).version;

    /* 0x00E50630: the target uid at request+0x90. */
    request.body.uid_name.target_uid.high = target_uid->high;
    request.body.uid_name.target_uid.low = target_uid->low;

    /* 0x00E5063A-0x00E50654 */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_ADD_HARD_LINKU >> 1).base_size + len),
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E5065C */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E50670-0x00E5067A */
        DIR_$OLD_ADD_HARD_LINKU(dir_uid, name, name_len, target_uid, status_ret);
    } else {
        /* 0x00E50682 */
        *status_ret = status;
    }
}
