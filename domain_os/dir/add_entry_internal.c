/*
 * DIR_$ADD_ENTRY_INTERNAL - shared body of DIR_$ADDU and DIR_$ROOT_ADDU
 *
 * Original address: 0x00E500B8
 * Original size: 232 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ADD_ENTRY_INTERNAL (0x00E500B8)
 *
 * Builds a DIR_OP_ADDU request and sends it through DIR_$DO_OP; on a
 * "bad reply"/"bad directory" reply it retries with DIR_$OLD_ADDU
 * (0x00E5694A) when flags is zero and DIR_$OLD_ROOT_ADDU (0x00E569AA)
 * otherwise.
 *
 * This routine does NOT load A5; it inherits the DIR module base from
 * DIR_$ADDU (0x00E501A0) / DIR_$ROOT_ADDU (0x00E52B8C), which is why the
 * table words come out of DIR_$OP_TAB record 0.
 *
 * Frame: `link.w A6,-0x1c0` - request base A6-0x1B8, reply A6-0x18
 * (0x14 bytes), received-length word A6-0x1BA.
 *
 * Parameters.  name_len is a WORD by value, so the arguments after it are
 * at odd-looking displacements:
 *   A6+0x08  dir_uid    (long)
 *   A6+0x0C  name       (long)
 *   A6+0x10  name_len   (word, by value)
 *   A6+0x12  file_uid   (long)
 *   A6+0x16  flags      (long, by value)
 *   A6+0x1A  status_ret (long)
 */
void DIR_$ADD_ENTRY_INTERNAL(uid_t *dir_uid, char *name, int16_t name_len,
                             uid_t *file_uid, uint32_t flags,
                             status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1BA: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    int16_t i;

    /* 0x00E500D8 / 0x00E500DC: a SIGNED word compare against 0xFF here
     * (`ble`), unlike the unsigned `bls` the other builders use. */
    if (name_len == 0 || name_len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E500EC: name length at request+0x8E. */
    request.body.add_entry.path_len = (uint16_t)name_len;
    /* 0x00E500F8-0x00E50106: name bytes at request+0x9C, i.e. after the
     * file uid and the flags longword. */
    for (i = 0; i < name_len; i++) {
        request.body.add_entry.name[i] = name[i];
    }

    /* 0x00E5010A-0x00E5011A */
    request.op = DIR_OP_ADDU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_ADDU >> 1).version;

    /* 0x00E50120: file uid at +0x90, 0x00E5012A: flags at +0x98. */
    request.body.add_entry.file_uid.high = file_uid->high;
    request.body.add_entry.file_uid.low = file_uid->low;
    request.body.add_entry.flags = flags;

    /* 0x00E5012E-0x00E50148 */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_ADDU >> 1).base_size + name_len),
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E50150 */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E50164: `tst.l D3` - the whole flags longword.  Both fallback
         * calls hand the callee the ADDRESS of this routine's own name_len
         * and flags parameter slots (`pea (0x10,A6)` / `pea (0x16,A6)`). */
        if (flags == 0) {
            DIR_$OLD_ADDU(dir_uid, name, &name_len, file_uid, status_ret);
        } else {
            DIR_$OLD_ROOT_ADDU(dir_uid, name, &name_len, file_uid, &flags,
                               status_ret);
        }
    } else {
        /* 0x00E50194 */
        *status_ret = status;
    }
}
