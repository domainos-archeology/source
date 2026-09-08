/*
 * DIR_$READ_LINKU - Read a soft link's target
 *
 * Original address: 0x00E4D6C0
 * Original size: 240 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$READ_LINKU (0x00E4D6C0)
 *
 * Builds a DIR_OP_READ_LINKU request and sends it through DIR_$DO_OP,
 * falling back to DIR_$OLD_READ_LINKU (0x00E577F4).
 *
 * Frame: `link.w A6,-0x1c0` - request base A6-0x1B8, reply A6-0x20
 * (0x1E bytes), received-length word A6-0x1BA.
 *
 * The body has the same shape as ADD_LINKU's: the length word at +0x8E, the
 * caller's buffer length at +0x90, the caller's buffer POINTER at +0x92 and
 * the leaf name at +0x96.  The link text itself is never copied in.
 *
 * Parameters (A6+0x08..A6+0x24):
 *   dir_uid    - UID of the directory
 *   name       - name of the link
 *   name_len   - pointer to the name length (1..0xFF)
 *   buf_len    - pointer to the caller's buffer length (must be > 0, SIGNED)
 *   target     - the caller's buffer; only its address goes on the wire
 *   target_len - out: the reply's length word at +0x14
 *   target_uid - out: the reply's uid at +0x16
 *   status_ret - out: status code
 */
void DIR_$READ_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     int16_t *buf_len, void *target, uint16_t *target_len,
                     uid_t *target_uid, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1BA: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    uint16_t len;
    int16_t i;

    /* 0x00E4D6E6-0x00E4D6EE */
    len = *name_len;
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* 0x00E4D6FA-0x00E4D702: the buffer length is stored into the request at
     * +0x90 FIRST and then tested with a SIGNED `bgt`, so zero and any
     * negative length are rejected. */
    request.body.add_link.target_len = (uint16_t)*buf_len;
    if (*buf_len <= 0) {
        /* 0x00E4D704 `move.l #0xe002e,(A2)` - "bad buffer size". */
        *status_ret = status_$naming_bad_buffer_size;
        return;
    }

    /* 0x00E4D70E: the NAME length is the word at +0x8E. */
    request.body.add_link.path_len = len;
    /* 0x00E4D716-0x00E4D726: name bytes at request+0x96. */
    for (i = 0; i < (int16_t)len; i++) {
        request.body.add_link.name[i] = name[i];
    }

    /* 0x00E4D72A-0x00E4D73A */
    request.op = DIR_OP_READ_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_READ_LINKU >> 1).version;

    /* 0x00E4D740: the caller's buffer POINTER at +0x92. */
    request.body.add_link.target_ptr = ARCH_PTR_TO_VA(target);

    /* 0x00E4D746-0x00E4D760 */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_READ_LINKU >> 1).base_size + len),
               0x1e, &response, &do_op_rcvd_len);

    /* 0x00E4D768-0x00E4D77A: the reply is unpacked unconditionally, before
     * the fallback test. */
    *status_ret = response.status;
    target_uid->high = response.entry.uid.high;
    target_uid->low = response.entry.uid.low;
    *target_len = response.entry.word;

    /* 0x00E4D77E */
    if (*status_ret == file_$bad_reply_received_from_remote_node ||
        *status_ret == status_$naming_bad_directory) {
        /* 0x00E4D790-0x00E4D7A0: seven longword pointers. */
        DIR_$OLD_READ_LINKU(dir_uid, name, name_len, target, target_len,
                            target_uid, status_ret);
    }
}
