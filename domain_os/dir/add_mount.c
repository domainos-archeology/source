/*
 * DIR_$ADD_MOUNT - Add a volume mount point to a directory
 *
 * Adds a mount point entry when a logical volume is mounted. Called
 * during volume mount (VOLX_$MOUNT) to create the directory entry
 * linking to the volume's root directory.
 *
 * Original address: 0x00E534B8
 * Original size: 96 bytes
 */

#include "dir/dir_internal.h"

/*
 * Request structure for ADD_MOUNT operation
 *
 * This structure matches the stack layout used by the original assembly.
 * The request has a sparse layout with data at specific offsets within
 * the request buffer.
 *
 * Stack layout in original:
 *   -0xb8: request buffer start (passed to DIR_$DO_OP)
 *   -0xb5: opcode (0x5a)
 *   -0xb4: dir_uid.high
 *   -0xb0: dir_uid.low
 *   -0xaa: my_host_id word (from DAT_00e7fd02)
 *   -0x2a: mount_uid.high
 *   -0x26: mount_uid.low
 *   -0x22: node_id (NODE_$ME)
 */
typedef struct {
    uint8_t   padding[3];       /* 0x00-0x02: Alignment padding */
    uint8_t   op;               /* 0x03: Operation code: DIR_OP_ADD_MOUNT */
    uid_t     dir_uid;          /* 0x04-0x0B: Directory UID to add mount to */
    uint16_t  reserved;         /* 0x0C-0x0D: Reserved */
    uint16_t  my_host_id;       /* 0x0E-0x0F: Host identifier word */
    uint8_t   gap[0x80];        /* 0x10-0x8F: Gap (matches original stack layout) */
    uid_t     mount_uid;        /* 0x90-0x97: Mount point UID (volume root) */
    uint32_t  node_id;          /* 0x98-0x9B: Node identifier (NODE_$ME) */
} Dir_$AddMountRequest;

/*
 * DIR_$ADD_MOUNT - Add a volume mount point to a directory
 *
 * Sends an ADD_MOUNT request to the directory server to create a
 * mount point entry for a newly mounted logical volume.
 *
 * Unlike most directory operations, DIR_$ADD_MOUNT does not have
 * an OLD_* fallback implementation - the ADD_MOUNT operation was
 * added in a later protocol version.
 *
 * Parameters:
 *   dir_uid    - UID of directory to add mount entry to
 *   mount_uid  - UID of the mounted volume's root directory
 *   status_ret - Output: status code
 */
void DIR_$ADD_MOUNT(uid_t *dir_uid, uid_t *mount_uid, status_$t *status_ret)
{
    Dir_$AddMountRequest request;
    Dir_$OpResponse response;

    /* Build the request */
    request.op = DIR_OP_ADD_MOUNT;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.my_host_id = DAT_00e7fd02;

    /* Copy mount point UID */
    request.mount_uid.high = mount_uid->high;
    request.mount_uid.low = mount_uid->low;

    /* Include this node's ID */
    request.node_id = NODE_$ME;

    /* Send the request
     * - &request.op: pointer to opcode (skipping padding)
     * - DAT_00e7fd06: request size (0x0c = 12 bytes for header)
     * - 0x14: response size (20 bytes)
     * - &response: response buffer
     * - &request: secondary buffer
     */
    DIR_$DO_OP(&request.op, DAT_00e7fd06, 0x14, &response, &request);

    /* Return status from response */
    *status_ret = response.status;
}
