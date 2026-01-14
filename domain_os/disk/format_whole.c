/*
 * DISK_$FORMAT_WHOLE - Format entire disk
 *
 * Formats all tracks on an assigned volume. The volume must be
 * assigned to the current process.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param status       Output: Status code
 */

#include "disk/disk_internal.h"

/* Volume table offsets */
#define DISK_MOUNT_STATE_OFFSET  0x90
#define DISK_MOUNT_PROC_OFFSET   0x92
#define DISK_DEV_INFO_OFFSET     0x7c

/* Event counter offsets in process table */
#define PROC_EC1_OFFSET  0x378
#define PROC_EC2_OFFSET  0x384

/* Volume table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Mount state 2 = assigned */
#define DISK_MOUNT_ASSIGNED  2

/* Process table base */
#define PROC_TABLE_BASE  ((uint8_t *)0x00e7a544)

void DISK_$FORMAT_WHOLE(uint16_t *vol_idx_ptr, status_$t *status)
{
    uint16_t vol_idx;
    int32_t offset;
    uint16_t mount_state;
    int16_t mount_proc;
    void *buffer;
    void *buffer_param;
    int32_t ec1, ec2;
    uint8_t *vol_entry;
    char result[4];

    vol_idx = *vol_idx_ptr;

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

    /* Allocate I/O request buffer */
    FUN_00e3be8a(1, 0, &buffer, &buffer_param);

    /* Get event counters from process table */
    ec1 = *(int32_t *)(PROC_TABLE_BASE + (int16_t)(PROC1_$CURRENT * 0x1c)) + 1;
    ec2 = *(int32_t *)(PROC_TABLE_BASE + (int16_t)(PROC1_$CURRENT * 0x1c) + 0xc) + 1;

    /* Set format whole operation (type 0x0a) */
    *(uint8_t *)((uintptr_t)buffer + 0x1f) &= 0xf0;
    *(uint8_t *)((uintptr_t)buffer + 0x1f) |= 0x0a;

    /* Perform the format I/O */
    DISK_$DO_IO(vol_entry + DISK_DEV_INFO_OFFSET, buffer, buffer, (void *)result);

    /* Check for error and signal event counters */
    if (result[0] < 0) {
        FUN_00e3c9fe((int16_t)(1 << (vol_idx & 0x1f)), &ec1, &ec2);
    }

    /* Return status from I/O result */
    *status = *(status_$t *)((uintptr_t)buffer + 0x0c);

    /* Free I/O request buffer */
    FUN_00e3c01a(1, buffer, buffer_param);
}
