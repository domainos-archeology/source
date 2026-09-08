/*
 * DIR_$FIX_DIR - Fix/repair a directory
 *
 * Builds a DIR_OP_FIX_DIR (0x48) request and sends it through DIR_$DO_OP.
 * The request has no body at all - DIR_$OP_TAB[15].base_size is 0x0000
 * (0x00E7FCBE) - so only the header fields at +0x03, +0x04 and +0x0E are
 * written.  On a "bad reply"/"bad directory" reply it retries with the
 * pre-DO_OP protocol via DIR_$OLD_FIX_DIR.
 *
 * Frame: `link.w A6,-0xac` (0x00E53E84) - request base A6-0xA8, reply base
 * A6-0x18, DIR_$DO_OP's received_len cell A6-0xAA.
 *
 * Original address: 0x00E53E84
 * Original size: 116 bytes
 */

#include "dir/dir_internal.h"

/*
 * Parameters (A6+0x08..A6+0x0C):
 *   dir_uid    - UID of the directory to fix; held in A2 across the call
 *   status_ret - out: status code; held in A3 across the call
 */
void DIR_$FIX_DIR(uid_t *dir_uid, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0xAA: DIR_$DO_OP's fifth argument, REM_FILE_$SEND_REQUEST's
     * `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;

    /* 0x00E53E9A: `move.b #0x48,(-0xa5,A6)` - the opcode at request+0x03. */
    request.op = DIR_OP_FIX_DIR;

    /* 0x00E53EA2 / 0x00E53EA6: the directory UID at request+0x04. */
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;

    /* 0x00E53EAA: `move.w (0x20ba,A5),(-0x9a,A6)` - DIR_$OP_TAB[15].version
     * (0x00E7FCBA) into request+0x0E. */
    request.version = DIR_$OP_REC(DIR_OP_FIX_DIR >> 1).version;

    /* 0x00E53EB0-0x00E53EC0: DIR_$OP_TAB[15].base_size (0x00E7FCBE, 0x0000)
     * as the body size and a 0x14-byte reply. */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_FIX_DIR >> 1).base_size,
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E53ECC: the whole longword at reply+0x04 is read into D0 once. */
    status = response.status;

    /* 0x00E53ED0 / 0x00E53ED8: `cmpi.l #0xf0003,D0` and `cmpi.l #0xe000d,D0`. */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E53EE0-0x00E53EE4: the old protocol gets the caller's own
         * uid pointer and status pointer. */
        DIR_$OLD_FIX_DIR(dir_uid, status_ret);
    } else {
        /* 0x00E53EEC: `move.l D0,(A3)`. */
        *status_ret = status;
    }
}
