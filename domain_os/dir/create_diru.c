/*
 * DIR_$CREATE_DIRU - Create a subdirectory
 *
 * Original address: 0x00E529EE
 * Original size: 198 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$CREATE_DIRU (0x00E529EE)
 *
 * Builds a DIR_OP_CREATE_DIRU request and sends it through DIR_$DO_OP,
 * falling back to DIR_$OLD_CREATE_DIRU (0x00E571AE).
 *
 * Frame: `link.w A6,-0x1b8` - request base A6-0x1B0, reply A6-0x20
 * (0x1C bytes), received-length word A6-0x1B2.
 *
 * Parameters (A6+0x08..A6+0x18):
 *   parent_uid  - UID of the parent directory
 *   name        - name for the new directory
 *   name_len    - pointer to the name length
 *   new_dir_uid - out: UID the reply carries at response+0x14
 *   status_ret  - out: status code
 */
void DIR_$CREATE_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                      uid_t *new_dir_uid, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1B2: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint16_t len;
    int16_t i;

    /* 0x00E52A10 */
    len = *name_len;

    /* 0x00E52A12 / 0x00E52A14 */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E52A24: length word at request+0x8E. */
    request.body.name.path_len = len;
    /* 0x00E52A2C-0x00E52A3A: name bytes at request+0x90. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.name.name[i] = name[i];
    }

    /* 0x00E52A3E-0x00E52A4E */
    request.op = DIR_OP_CREATE_DIRU;
    request.uid.high = parent_uid->high;
    request.uid.low = parent_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_CREATE_DIRU >> 1).version;

    /* 0x00E52A54-0x00E52A6E */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_CREATE_DIRU >> 1).base_size + len),
               0x1c, &response, &do_op_rcvd_len);

    /* 0x00E52A76 */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E52A8A-0x00E52A94 */
        DIR_$OLD_CREATE_DIRU(parent_uid, name, name_len, new_dir_uid, status_ret);
    } else {
        /* 0x00E52A9C: status first, then the UID at response+0x14
         * (`lea (-0xc,A6),A0` with the reply based at A6-0x20). */
        *status_ret = status;
        new_dir_uid->high = response.uid.high;
        new_dir_uid->low = response.uid.low;
    }
}
