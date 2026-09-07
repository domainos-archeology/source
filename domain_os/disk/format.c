/*
 * DISK_$FORMAT - Format a single track
 *
 * Formats a specific track on an assigned volume. Validates that
 * the device supports track-level formatting.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param cyl_ptr      Pointer to cylinder number
 * @param head_ptr     Pointer to head/track number
 * @param status       Output: Status code
 */

#include "disk/disk_internal.h"
#include "arch/arch.h"

/* disk_$volume_t, DISK_VOL(), VALID_VOL_MASK and DISK_MOUNT_ASSIGNED come
 * from disk/disk_internal.h.  The head divisor is the volume's head count at
 * +0x9e (0xe3d45c divu.w (0x9e,A2)) and the partition table is part_volx
 * (+0xb2). */

/* Event counter offsets in process table */
#define PROC_EC1_OFFSET  0x378
#define PROC_EC2_OFFSET  0x384

/* Device flags */
#define DEV_FLAG_NO_TRACK_FORMAT  0x200

/* Process table base */
#define PROC_TABLE_BASE  ((uint8_t *)0x00e7a544)

void DISK_$FORMAT(uint16_t *vol_idx_ptr, uint16_t *cyl_ptr, uint16_t *head_ptr,
                  status_$t *status)
{
    uint16_t vol_idx;
    uint16_t cylinder;
    uint16_t head;
    uint16_t mount_state;
    int16_t mount_proc;
    /* Two four-byte VA cells; disk_$get_qblks_internal stores each with one
     * `move.l` (0x00E3BF7E, 0x00E3BFB8). */
    uint32_t buffer_va;
    uint32_t buffer_param_va;
    void *buffer;
    void *buffer_param;
    int32_t ec1, ec2;
    disk_$volume_t *vol;
    void *dev_info;
    uint16_t dev_flags;
    uint16_t heads_per_part;
    uint16_t partition_idx;
    uint16_t partition_vol;
    char result[14];

    vol_idx = *vol_idx_ptr;
    cylinder = *cyl_ptr;
    head = *head_ptr;

    /* Validate volume index (must be 1-10) */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    vol = DISK_VOL(vol_idx);

    /* Check mount state and ownership */
    mount_state = vol->mount_state;
    mount_proc = vol->mount_proc;

    if (mount_state != DISK_MOUNT_ASSIGNED || mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* Get device info and check for track format support */
    dev_info = vol->dev_info;
    dev_flags = *(uint16_t *)((uintptr_t)dev_info + 8);

    if ((dev_flags & DEV_FLAG_NO_TRACK_FORMAT) != 0) {
        *status = status_$disk_illegal_request_for_device;
        return;
    }

    /* Allocate I/O request buffer */
    disk_$get_qblks_internal(1, 0, &buffer_va, &buffer_param_va);
    buffer = ARCH_VA_TO_PTR(buffer_va);
    buffer_param = ARCH_VA_TO_PTR(buffer_param_va);

    /* Get event counters from process table */
    ec1 = *(int32_t *)(PROC_TABLE_BASE + (int16_t)(PROC1_$CURRENT * 0x1c)) + 1;
    ec2 = *(int32_t *)(PROC_TABLE_BASE + (int16_t)(PROC1_$CURRENT * 0x1c) + 0xc) + 1;

    /* Calculate partition index from head number (0xe3d45c) */
    heads_per_part = vol->num_heads;
    partition_idx = (head / heads_per_part) + 1;

    /* Get the partition volume from the partition table (0xe3d46c) */
    partition_vol = vol->part_volx[partition_idx];

    /* Validate partition index and volume */
    if (partition_idx > 8 ||
        (((uint32_t)1 << (partition_vol & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        disk_$rtn_qblks_internal(1, buffer, buffer_param);
        return;
    }

    /* Set up I/O request buffer for format */
    *(uint16_t *)((uintptr_t)buffer + 4) = cylinder;
    *(uint8_t *)((uintptr_t)buffer + 6) = (uint8_t)(head % heads_per_part);
    *(uint8_t *)((uintptr_t)buffer + 7) = 1;

    /* Set format track operation (type 0x03) */
    *(uint8_t *)((uintptr_t)buffer + 0x1f) &= 0xf0;
    *(uint8_t *)((uintptr_t)buffer + 0x1f) |= 0x03;

    /* Get partition volume descriptor and perform format I/O.  DISK_$DO_IO
     * receives the descriptor base (pea (0x7c,A2) in the original). */
    DISK_$DO_IO(DISK_VOL(partition_vol), buffer, buffer, (void *)result);

    /* Check for error and signal event counters */
    if (result[0] < 0) {
        disk_$wait_io((int16_t)(1 << (partition_vol & 0x1f)), &ec1, &ec2);
    }

    /* Return status from I/O result */
    *status = *(status_$t *)((uintptr_t)buffer + 0x0c);

    /* Free I/O request buffer */
    disk_$rtn_qblks_internal(1, buffer, buffer_param);
}
