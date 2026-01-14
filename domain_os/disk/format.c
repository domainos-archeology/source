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

/* Volume table offsets */
#define DISK_MOUNT_STATE_OFFSET  0x90
#define DISK_MOUNT_PROC_OFFSET   0x92
#define DISK_DEV_INFO_OFFSET     0x94
#define DISK_DEV_DATA_OFFSET     0x7c
#define DISK_SECTORS_PER_TRACK   0x9e
#define DISK_PARTITION_TABLE     0xb2

/* Event counter offsets in process table */
#define PROC_EC1_OFFSET  0x378
#define PROC_EC2_OFFSET  0x384

/* Volume table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Mount state 2 = assigned */
#define DISK_MOUNT_ASSIGNED  2

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
    int32_t offset;
    uint16_t mount_state;
    int16_t mount_proc;
    void *buffer;
    void *buffer_param;
    int32_t ec1, ec2;
    uint8_t *vol_entry;
    void *dev_info;
    uint16_t dev_flags;
    uint16_t sectors_per_track;
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

    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    vol_entry = DISK_VOLUME_BASE + offset;

    /* Check mount state and ownership */
    mount_state = *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET);
    mount_proc = *(int16_t *)(vol_entry + DISK_MOUNT_PROC_OFFSET);

    if (mount_state != DISK_MOUNT_ASSIGNED || mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* Get device info and check for track format support */
    dev_info = *(void **)(vol_entry + DISK_DEV_INFO_OFFSET);
    dev_flags = *(uint16_t *)((uintptr_t)dev_info + 8);

    if ((dev_flags & DEV_FLAG_NO_TRACK_FORMAT) != 0) {
        *status = status_$disk_illegal_request_for_device;
        return;
    }

    /* Allocate I/O request buffer */
    FUN_00e3be8a(1, 0, &buffer, &buffer_param);

    /* Get event counters from process table */
    ec1 = *(int32_t *)(PROC_TABLE_BASE + (int16_t)(PROC1_$CURRENT * 0x1c)) + 1;
    ec2 = *(int32_t *)(PROC_TABLE_BASE + (int16_t)(PROC1_$CURRENT * 0x1c) + 0xc) + 1;

    /* Calculate partition index from head number */
    sectors_per_track = *(uint16_t *)(vol_entry + DISK_SECTORS_PER_TRACK);
    partition_idx = (head / sectors_per_track) + 1;

    /* Get the partition volume from the partition table */
    partition_vol = *(uint16_t *)(vol_entry + DISK_PARTITION_TABLE + partition_idx * 2);

    /* Validate partition index and volume */
    if (partition_idx > 8 ||
        (((uint32_t)1 << (partition_vol & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        FUN_00e3c01a(1, buffer, buffer_param);
        return;
    }

    /* Set up I/O request buffer for format */
    *(uint16_t *)((uintptr_t)buffer + 4) = cylinder;
    *(uint8_t *)((uintptr_t)buffer + 6) = (uint8_t)(head % sectors_per_track);
    *(uint8_t *)((uintptr_t)buffer + 7) = 1;

    /* Set format track operation (type 0x03) */
    *(uint8_t *)((uintptr_t)buffer + 0x1f) &= 0xf0;
    *(uint8_t *)((uintptr_t)buffer + 0x1f) |= 0x03;

    /* Get partition volume entry and perform format I/O */
    offset = (int16_t)(partition_vol * DISK_VOLUME_SIZE);
    vol_entry = DISK_VOLUME_BASE + offset;

    DISK_$DO_IO(vol_entry + DISK_DEV_DATA_OFFSET, buffer, buffer, (void *)result);

    /* Check for error and signal event counters */
    if (result[0] < 0) {
        FUN_00e3c9fe((int16_t)(1 << (partition_vol & 0x1f)), &ec1, &ec2);
    }

    /* Return status from I/O result */
    *status = *(status_$t *)((uintptr_t)buffer + 0x0c);

    /* Free I/O request buffer */
    FUN_00e3c01a(1, buffer, buffer_param);
}
