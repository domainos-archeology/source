/*
 * DIR_$ADD_MOUNT - Add a volume mount point to a directory
 *
 * Adds a mount point entry when a logical volume is mounted.  Called
 * during volume mount to record the mounted volume's root directory in
 * the parent directory's mount table.
 *
 * Builds a DIR_OP_ADD_MOUNT (0x5A) request and sends it through
 * DIR_$DO_OP.  Unlike most directory operations there is no OLD_*
 * fallback: the reply status is returned unconditionally.
 *
 * Frame: `link.w A6,-0xbc` (0x00E534B8) - request base A6-0xB8, reply
 * base A6-0x18, DIR_$DO_OP's received_len cell A6-0xBA.
 *
 * Original address: 0x00E534B8
 * Original size: 96 bytes
 */

#include "dir/dir_internal.h"

/*
 * Parameters (A6+0x08..A6+0x10):
 *   dir_uid    - UID of the directory the mount entry is added to
 *   mount_uid  - UID of the mounted volume's root directory
 *   status_ret - out: status code
 */
void DIR_$ADD_MOUNT(uid_t *dir_uid, uid_t *mount_uid, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0xBA: DIR_$DO_OP's fifth argument, REM_FILE_$SEND_REQUEST's
     * `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;

    /* 0x00E534C4: `move.b #0x5a,(-0xb5,A6)` - the opcode at request+0x03. */
    request.op = DIR_OP_ADD_MOUNT;

    /* 0x00E534CE / 0x00E534D2: the directory UID at request+0x04. */
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;

    /* 0x00E534D6: `move.w (0x2102,A5),(-0xaa,A6)` - DIR_$OP_TAB[24].version
     * (0x00E7FD02) into request+0x0E. */
    request.version = DIR_$OP_REC(DIR_OP_ADD_MOUNT >> 1).version;

    /* 0x00E534E0 / 0x00E534E4: the mounted volume's root UID at
     * request+0x8E (A6-0x2A). */
    request.body.mount.mount_uid.high = mount_uid->high;
    request.body.mount.mount_uid.low = mount_uid->low;

    /* 0x00E534E8: `move.l (0x00e245a4).l,(-0x22,A6)` - this node's ID at
     * request+0x96 (A6-0x22). */
    request.body.mount.node_id = NODE_$ME;

    /* 0x00E534F0-0x00E53504: DIR_$OP_TAB[24].base_size (0x00E7FD06, 0x000C)
     * as the body size and a 0x14-byte reply. */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_ADD_MOUNT >> 1).base_size,
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E5350C: `move.l (-0x14,A6),(A0)` - the longword at reply+0x04. */
    *status_ret = response.status;
}
