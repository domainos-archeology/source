/*
 * DIR_$CNAMEU - Change name (rename) an entry
 *
 * Original address: 0x00E51B68
 * Original size: 258 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$CNAMEU (0x00E51B68)
 *
 * Builds a DIR_OP_CNAMEU request and sends it through DIR_$DO_OP,
 * falling back to DIR_$OLD_CNAMEU (0x00E57562).
 *
 * Frame: `link.w A6,-0x2b8` - request base A6-0x2B0, reply A6-0x18
 * (0x14 bytes), received-length word A6-0x2B2.
 *
 * Body: old length at +0x8E, new length at +0x90, then the old name at
 * +0x92 followed immediately by the new name.  The old name's offset is
 * a compile-time +0x92 while the new name's is computed at run time from
 * DIR_$OP_TAB[4].base_size (0x00E7FC66, value 4) - the two agree exactly
 * because base_size is the fixed part of the body.
 *
 * Parameters (A6+0x08..A6+0x1C):
 *   dir_uid      - UID of the directory holding the entry
 *   old_name     - current name
 *   old_name_len - pointer to the current name length
 *   new_name     - new name
 *   new_name_len - pointer to the new name length
 *   status_ret   - out: status code
 */
void DIR_$CNAMEU(uid_t *dir_uid, char *old_name, uint16_t *old_name_len,
                 char *new_name, uint16_t *new_name_len, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x2B2: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint16_t olen, nlen;
    int16_t i;

    /* 0x00E51B90-0x00E51BA2: both lengths must be 1..0xFF. */
    if (*old_name_len == 0 || *new_name_len == 0 ||
        *old_name_len > DIR_MAX_LEAF_LEN || *new_name_len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E51BB0: old length at request+0x8E. */
    request.body.cname.old_len = *old_name_len;
    olen = request.body.cname.old_len;
    /* 0x00E51BBC-0x00E51BCA: old name bytes at request+0x92. */
    for (i = 0; i < (int16_t)olen; i++) {
        request.body.cname.name[i] = old_name[i];
    }

    /* 0x00E51BCE: new length at request+0x90. */
    request.body.cname.new_len = *new_name_len;
    nlen = request.body.cname.new_len;
    /* 0x00E51BDE-0x00E51BF4: the destination index is formed from the body
     * base (+0x8E) as base_size + old_len + i, so the new name runs on
     * directly from the old one. */
    for (i = 0; i < (int16_t)nlen; i++) {
        request.body.raw[DIR_$OP_REC(DIR_OP_CNAMEU >> 1).base_size + olen + i] =
            new_name[i];
    }

    /* 0x00E51BF8-0x00E51C08 */
    request.op = DIR_OP_CNAMEU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_CNAMEU >> 1).version;

    /* 0x00E51C0E-0x00E51C2C */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_CNAMEU >> 1).base_size + olen + nlen),
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E51C34 */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E51C48-0x00E51C54 */
        DIR_$OLD_CNAMEU(dir_uid, old_name, old_name_len,
                        new_name, new_name_len, status_ret);
    } else {
        /* 0x00E51C5C */
        *status_ret = status;
    }
}
