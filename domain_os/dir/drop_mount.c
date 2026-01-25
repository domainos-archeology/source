/*
 * DIR_$DROP_MOUNT - Remove a volume mount point from a directory
 *
 * Removes a mount point entry that was created when a logical volume
 * was mounted. Called during volume dismount (VOLX_$DISMOUNT) to clean
 * up the directory entry linking to the volume's root.
 *
 * Original address: 0x00E53518
 * Original size: 96 bytes
 */

#include "dir/dir_internal.h"

/*
 * Request structure for DROP_MOUNT operation
 *
 * This structure matches the stack layout used by the original assembly.
 * The request consists of a 12-byte header (opcode + mount point UID)
 * plus additional fields for the directory UID and logical volume number.
 *
 * Stack layout in original:
 *   -0xb8: request buffer start (passed to DIR_$DO_OP)
 *   -0xb5: opcode (0x5c)
 *   -0xb4: mount_point_uid.high
 *   -0xb0: mount_point_uid.low
 *   -0xaa: my_host_id word (from DAT_00e7fd0a, typically 0)
 *   -0x2a: dir_uid.high
 *   -0x26: dir_uid.low
 *   -0x22: lv_num
 */
typedef struct {
    uint8_t   padding[3];       /* 0x00-0x02: Alignment padding */
    uint8_t   op;               /* 0x03: Operation code: DIR_OP_DROP_MOUNT */
    uid_t     mount_point_uid;  /* 0x04-0x0B: Mount point directory UID */
    uint16_t  my_host_id;       /* 0x0C-0x0D: Host identifier word */
    uint8_t   gap[0x80];        /* 0x0E-0x8D: Gap (matches original stack layout) */
    uid_t     dir_uid;          /* 0x8E-0x95: Directory containing mount entry */
    uint32_t  lv_num;           /* 0x96-0x99: Logical volume number */
} Dir_$DropMountRequest;

/*
 * DIR_$DROP_MOUNT - Remove a volume mount point from a directory
 *
 * Sends a DROP_MOUNT request to the directory server to remove the
 * mount point entry for a dismounted logical volume.
 *
 * Unlike most directory operations, DIR_$DROP_MOUNT does not have
 * an OLD_* fallback implementation - the DROP_MOUNT operation was
 * added in a later protocol version.
 *
 * Parameters:
 *   mount_point_uid - UID of the mount point directory
 *   dir_uid         - UID of directory containing the mount entry
 *   lv_num          - Pointer to logical volume number (or zero)
 *   status_ret      - Output: status code
 */
void DIR_$DROP_MOUNT(uid_t *mount_point_uid, uid_t *dir_uid, uint32_t *lv_num,
                     status_$t *status_ret)
{
    Dir_$DropMountRequest request;
    Dir_$OpResponse response;

    /* Build the request */
    request.op = DIR_OP_DROP_MOUNT;
    request.mount_point_uid.high = mount_point_uid->high;
    request.mount_point_uid.low = mount_point_uid->low;
    request.my_host_id = DAT_00e7fd0a;

    /* Copy directory UID */
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;

    /* Copy logical volume number */
    request.lv_num = *lv_num;

    /* Send the request
     * - &request.op: pointer to opcode (skipping padding)
     * - DAT_00e7fd0e: request size (0x0c = 12 bytes for header)
     * - 0x14: response size (20 bytes)
     * - &response: response buffer
     * - &request: secondary buffer
     */
    DIR_$DO_OP(&request.op, DAT_00e7fd0e, 0x14, &response, &request);

    /* Return status from response */
    *status_ret = response.status;
}
