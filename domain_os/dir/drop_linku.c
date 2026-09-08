/*
 * DIR_$DROP_LINKU - Drop a soft link
 *
 * Original address: 0x00E517F6
 * Original size: 198 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DROP_LINKU (0x00E517F6)
 *
 * Builds a DIR_OP_DROP_LINKU request and sends it through DIR_$DO_OP.
 * On a "bad reply"/"bad directory" reply it retries with the pre-DO_OP
 * implementation DIR_$OLD_DROP_LINKU (0x00E57924).
 *
 * Frame: `link.w A6,-0x1b8` - request base A6-0x1B0, reply A6-0x20
 * (0x1C bytes), received-length word A6-0x1B2.
 *
 * Parameters (A6+0x08..A6+0x18):
 *   dir_uid    - UID of parent directory
 *   name       - name of the link to drop
 *   name_len   - pointer to the name length
 *   target_uid - out: UID the reply carries at response+0x14
 *   status_ret - out: status code
 */
void DIR_$DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     uid_t *target_uid, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1B2: DIR_$DO_OP's fifth argument, REM_FILE_$SEND_REQUEST's
     * `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint16_t len;
    int16_t i;

    /* 0x00E51818: the length word is read once into D0w. */
    len = *name_len;

    /* 0x00E5181A / 0x00E5181C: reject 0 and anything above 0xFF. */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E5182C: the length word lives in the request at +0x8E. */
    request.body.name.path_len = len;
    /* 0x00E51834-0x00E51842: `dbf` copy of len bytes to request+0x90. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.name.name[i] = name[i];
    }

    /* 0x00E51846-0x00E51856 */
    request.op = DIR_OP_DROP_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_DROP_LINKU >> 1).version;

    /* 0x00E5185C-0x00E51876 */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_DROP_LINKU >> 1).base_size + len),
               0x1c, &response, &do_op_rcvd_len);

    /* 0x00E5187E: the whole longword at response+0x04. */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E51892-0x00E5189C */
        DIR_$OLD_DROP_LINKU(dir_uid, name, name_len, target_uid, status_ret);
    } else {
        /* 0x00E518A4: the returned UID is the 8 bytes at response+0x14,
         * not the +0x16 variant DIR_$READ_LINKU uses.  The UID is stored
         * first, the status afterwards. */
        target_uid->high = response.uid.high;
        target_uid->low = response.uid.low;
        *status_ret = status;
    }
}
