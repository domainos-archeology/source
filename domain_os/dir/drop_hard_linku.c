/*
 * DIR_$DROP_HARD_LINKU - Drop a hard link
 *
 * Original address: 0x00E516FC
 * Original size: 250 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DROP_HARD_LINKU (0x00E516FC)
 *
 * Builds a DIR_OP_DROP_HARD_LINKU request and sends it through DIR_$DO_OP.
 * When the reply came from a remote node and carries a non-NIL uid, that
 * object is flushed with AST_$COND_FLUSH and the routine returns without
 * considering the legacy fallback DIR_$OLD_DROP_HARD_LINKU (0x00E56ACA).
 *
 * Frame: `link.w A6,-0x1cc` - request base A6-0x1B8, reply A6-0x20
 * (0x1C bytes), received-length word A6-0x1C6.
 *
 * Parameters (A6+0x08..A6+0x18):
 *   dir_uid    - UID of the directory
 *   name       - name of the link to drop
 *   name_len   - pointer to the name length
 *   flags      - pointer to the flags word copied into request+0x90
 *   status_ret - out: status code
 */
void DIR_$DROP_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                          uint16_t *flags, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1C6: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint32_t flush_flags;       /* A6-0x1C0 */
    status_$t flush_status;     /* A6-0x1C4 */
    uint16_t len;
    int16_t i;

    /* 0x00E5171E-0x00E51726 */
    len = *name_len;
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E51732: length at request+0x8E. */
    request.body.word_name.path_len = len;
    /* 0x00E5173A-0x00E51748: name bytes at request+0x92. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.word_name.name[i] = name[i];
    }

    /* 0x00E5174C-0x00E5175C */
    request.op = DIR_OP_DROP_HARD_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_DROP_HARD_LINKU >> 1).version;

    /* 0x00E51762: `move.w (A0),(-0x128,A6)` - the flags WORD at +0x90. */
    request.body.word_name.flags = *flags;

    /* 0x00E51768-0x00E51782 */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_DROP_HARD_LINKU >> 1).base_size + len),
               0x1c, &response, &do_op_rcvd_len);

    /* 0x00E5178A: the status is copied out FIRST, before any test. */
    status = response.status;
    *status_ret = status;

    /* 0x00E5178E: `btst.b #0x0,(-0xd,A6)` - reply+0x13 bit 0. */
    if ((response.f18[DIR_RESP_REMOTE_FLAG_BYTE] & DIR_RESP_REMOTE_FLAG) != 0 &&
        status == status_$ok) {
        /* 0x00E5179C: the reply's uid at +0x14 against UID_$NIL. */
        if (response.uid.high != UID_$NIL.high ||
            response.uid.low != UID_$NIL.low) {
            /* 0x00E517B0-0x00E517C0 */
            flush_flags = 0;
            AST_$COND_FLUSH(&response.uid, &flush_flags, &flush_status);
            return;
        }
    }

    /* 0x00E517C8 */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E517DC-0x00E517E6 */
        DIR_$OLD_DROP_HARD_LINKU(dir_uid, name, name_len, flags, status_ret);
    }
}
