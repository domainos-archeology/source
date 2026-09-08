/*
 * DIR_$DROP_DIRU - Drop (delete) a subdirectory entry
 *
 * Original address: 0x00E52AB4
 * Original size: 178 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DROP_DIRU (0x00E52AB4)
 *
 * Builds a DIR_OP_DROP_DIRU request and sends it through DIR_$DO_OP,
 * falling back to DIR_$OLD_DROP_DIRU (0x00E5734C).
 *
 * Frame: `link.w A6,-0x1b0` - request base A6-0x1A8, reply A6-0x18
 * (0x14 bytes), received-length word A6-0x1AA.
 *
 * Parameters (A6+0x08..A6+0x14):
 *   parent_uid - UID of the parent directory
 *   name       - name of the directory to drop
 *   name_len   - pointer to the name length
 *   status_ret - out: status code
 */
void DIR_$DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                    status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1AA: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint16_t len;
    int16_t i;

    /* 0x00E52AD2-0x00E52ADA */
    len = *name_len;
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E52AE4: length at request+0x8E. */
    request.body.name.path_len = len;
    /* 0x00E52AEC-0x00E52AFA: name bytes at request+0x90. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.name.name[i] = name[i];
    }

    /* 0x00E52AFE-0x00E52B0E */
    request.op = DIR_OP_DROP_DIRU;
    request.uid.high = parent_uid->high;
    request.uid.low = parent_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_DROP_DIRU >> 1).version;

    /* 0x00E52B14-0x00E52B2E */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_DROP_DIRU >> 1).base_size + len),
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E52B36 */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E52B4A-0x00E52B52: four longwords. */
        DIR_$OLD_DROP_DIRU(parent_uid, name, name_len, status_ret);
    } else {
        /* 0x00E52B5A */
        *status_ret = status;
    }
}
