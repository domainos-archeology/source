/*
 * DIR_$ADD_BAKU - Add a backup entry
 *
 * Original address: 0x00E50C60
 * Original size: 254 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ADD_BAKU (0x00E50C60)
 *
 * Builds a DIR_OP_ADD_BAKU request and sends it through DIR_$DO_OP.  When
 * the reply came from a remote node and carries a non-NIL uid, the object
 * it names is flushed with AST_$COND_FLUSH and the routine returns without
 * ever considering the legacy fallback DIR_$OLD_ADD_BAKU (0x00E56E3E).
 *
 * Frame: `link.w A6,-0x1cc` - request base A6-0x1B8, reply A6-0x20
 * (0x1C bytes), received-length word A6-0x1C6.
 *
 * Parameters (A6+0x08..A6+0x18):
 *   dir_uid    - UID of the directory
 *   name       - name of the entry being backed up
 *   name_len   - pointer to the name length
 *   backup_uid - UID of the backup object
 *   status_ret - out: status code
 */
void DIR_$ADD_BAKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                   uid_t *backup_uid, status_$t *status_ret)
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

    /* 0x00E50C82-0x00E50C8A */
    len = *name_len;
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E50C96: length at request+0x8E. */
    request.body.uid_name.path_len = len;
    /* 0x00E50C9E-0x00E50CAC: name bytes at request+0x98. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.uid_name.name[i] = name[i];
    }

    /* 0x00E50CB0-0x00E50CC0 */
    request.op = DIR_OP_ADD_BAKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_ADD_BAKU >> 1).version;

    /* 0x00E50CC6: the backup uid at request+0x90. */
    request.body.uid_name.target_uid.high = backup_uid->high;
    request.body.uid_name.target_uid.low = backup_uid->low;

    /* 0x00E50CD0-0x00E50CEA */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_ADD_BAKU >> 1).base_size + len),
               0x1c, &response, &do_op_rcvd_len);

    /* 0x00E50CF2: the status is copied out FIRST, before any test. */
    status = response.status;
    *status_ret = status;

    /* 0x00E50CF6: `btst.b #0x0,(-0xd,A6)` - reply+0x13 bit 0, "the reply
     * came from a remote node". */
    if ((response.f18[DIR_RESP_REMOTE_FLAG_BYTE] & DIR_RESP_REMOTE_FLAG) != 0 &&
        status == status_$ok) {
        /* 0x00E50D04: the reply's uid at +0x14 against UID_$NIL. */
        if (response.uid.high != UID_$NIL.high ||
            response.uid.low != UID_$NIL.low) {
            /* 0x00E50D18-0x00E50D28 */
            flush_flags = 0;
            AST_$COND_FLUSH(&response.uid, &flush_flags, &flush_status);
            return;
        }
    }

    /* 0x00E50D30 */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E50D44-0x00E50D4E */
        DIR_$OLD_ADD_BAKU(dir_uid, name, name_len, backup_uid, status_ret);
    }
}
