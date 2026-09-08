/*
 * DIR_$DROP_MOUNT - Remove a volume mount point from a directory
 *
 * Removes the mount point entry a DIR_$ADD_MOUNT created.  Called during
 * volume dismount.
 *
 * Builds a DIR_OP_DROP_MOUNT (0x5C) request and sends it through
 * DIR_$DO_OP.  Like DIR_$ADD_MOUNT there is no OLD_* fallback.
 *
 * Frame: `link.w A6,-0xbc` (0x00E53518) - request base A6-0xB8, reply
 * base A6-0x18, DIR_$DO_OP's received_len cell A6-0xBA.
 *
 * Original address: 0x00E53518
 * Original size: 96 bytes
 */

#include "dir/dir_internal.h"

/*
 * Parameters (A6+0x08..A6+0x14):
 *   mount_point_uid - UID of the directory holding the mount entry; it is
 *                     the request header UID at request+0x04
 *   dir_uid         - UID of the mounted volume's root directory; the body
 *                     UID at request+0x8E, which DIR_$DO_OP's case 0x5C
 *                     hands dir_$do_op_drop_mount as its first argument
 *   lv_num          - pointer to the longword stored at request+0x96, the
 *                     cell dir_$do_op_drop_mount takes as its node_id
 *   status_ret      - out: status code
 */
void DIR_$DROP_MOUNT(uid_t *mount_point_uid, uid_t *dir_uid,
                     uint32_t *lv_num, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0xBA: DIR_$DO_OP's fifth argument, REM_FILE_$SEND_REQUEST's
     * `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;

    /* 0x00E53524: `move.b #0x5c,(-0xb5,A6)` - the opcode at request+0x03. */
    request.op = DIR_OP_DROP_MOUNT;

    /* 0x00E5352E / 0x00E53532: the mount point UID at request+0x04. */
    request.uid.high = mount_point_uid->high;
    request.uid.low = mount_point_uid->low;

    /* 0x00E53536: `move.w (0x210a,A5),(-0xaa,A6)` - DIR_$OP_TAB[25].version
     * (0x00E7FD0A) into request+0x0E. */
    request.version = DIR_$OP_REC(DIR_OP_DROP_MOUNT >> 1).version;

    /* 0x00E53540 / 0x00E53544: the body UID at request+0x8E (A6-0x2A). */
    request.body.mount.mount_uid.high = dir_uid->high;
    request.body.mount.mount_uid.low = dir_uid->low;

    /* 0x00E53548 / 0x00E5354C: `movea.l (0x10,A6),A0` then
     * `move.l (A0),(-0x22,A6)` - the caller's longword at request+0x96. */
    request.body.mount.node_id = *lv_num;

    /* 0x00E53550-0x00E53560: DIR_$OP_TAB[25].base_size (0x00E7FD0E, 0x000C)
     * as the body size and a 0x14-byte reply. */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_DROP_MOUNT >> 1).base_size,
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E5356C: `move.l (-0x14,A6),(A0)` - the longword at reply+0x04. */
    *status_ret = response.status;
}
