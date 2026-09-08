/*
 * DIR_$ADD_LINKU - Add a soft/symbolic link
 *
 * Original address: 0x00E5068E
 * Original size: 258 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ADD_LINKU (0x00E5068E)
 *
 * Builds a DIR_OP_ADD_LINKU request and sends it through DIR_$DO_OP,
 * falling back to DIR_$OLD_ADD_LINKU (0x00E576EA).
 *
 * Frame: `link.w A6,-0x1b8` - request base A6-0x1B0, reply A6-0x18
 * (0x14 bytes), received-length word A6-0x1B2.
 *
 * The request body carries the link name inline but only the *pointer*
 * to the target text (0x00E5071A `move.l (0x14,A6),(-0x11e,A6)`); the
 * image never copies the target bytes into the buffer.
 *
 * Parameters (A6+0x08..A6+0x1C):
 *   dir_uid    - UID of the directory to hold the link
 *   name       - name for the link
 *   name_len   - pointer to the name length (1..0xFF)
 *   target     - target pathname text
 *   target_len - pointer to the target length (1..0x3FF)
 *   status_ret - out: status code
 */
void DIR_$ADD_LINKU(uid_t *dir_uid, char *name, int16_t *name_len,
                    void *target, uint16_t *target_len, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1B2: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint16_t len, tlen;
    int16_t i;
    int32_t total_size;

    /* 0x00E506B0 */
    len = (uint16_t)*name_len;

    /* 0x00E506B2 / 0x00E506B4 */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E506C6 / 0x00E506CA */
    tlen = *target_len;
    if (tlen == 0 || tlen > DIR_MAX_LINK_LEN) {
        *status_ret = status_$naming_invalid_link;
        return;
    }

    /* 0x00E506D0-0x00E506EE: the wire length the request would need,
     * (name_len + target_len) + base_size + 0x8E, capped at 0x500. */
    total_size = (int32_t)len + (int32_t)tlen;
    total_size += (int32_t)(int16_t)DIR_$OP_REC(DIR_OP_ADD_LINKU >> 1).base_size;
    total_size += 0x8e;
    if (total_size > 0x500) {
        *status_ret = status_$naming_invalid_link;
        return;
    }

    /* 0x00E506FC: name length at request+0x8E. */
    request.body.add_link.path_len = len;
    /* 0x00E50704-0x00E50712: name bytes at request+0x96. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.add_link.name[i] = name[i];
    }

    /* 0x00E50716: the target length word is re-read from the caller's
     * cell; 0x00E5071A stores the caller's target POINTER at +0x92. */
    request.body.add_link.target_len = *target_len;
    request.body.add_link.target_ptr = ARCH_PTR_TO_VA(target);

    /* 0x00E50720-0x00E50730 */
    request.op = DIR_OP_ADD_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_ADD_LINKU >> 1).version;

    /* 0x00E50736-0x00E50750 */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_ADD_LINKU >> 1).base_size + len),
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E50758 */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E5076C-0x00E5077A */
        DIR_$OLD_ADD_LINKU(dir_uid, name, name_len, target, target_len, status_ret);
    } else {
        /* 0x00E50782 */
        *status_ret = status;
    }
}
