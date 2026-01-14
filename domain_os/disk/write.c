/*
 * DISK_$WRITE - Write data to disk
 *
 * Writes data to a mounted volume. Validates that the volume
 * is properly mounted before performing the I/O.
 *
 * @param vol_idx  Volume index
 * @param buffer   Data buffer
 * @param daddr    Disk address
 * @param count    Number of blocks
 * @param status   Output: Status code
 */

#include "disk.h"

/* Mount state offset within volume entry */
#define DISK_MOUNT_STATE_OFFSET  0x90

/* Volume entry table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* I/O operation codes */
#define DISK_OP_WRITE  1

/* External I/O function */
extern status_$t DISK_IO(int16_t op, int16_t vol_idx, void *daddr,
                         void *buffer, void *count);

void DISK_$WRITE(int16_t vol_idx, void *buffer, void *daddr, void *count,
                 status_$t *status)
{
    int32_t offset;
    uint16_t mount_state;

    /* Calculate offset to mount state */
    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);

    /* Get mount state */
    mount_state = *(uint16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_STATE_OFFSET);

    if (mount_state != DISK_MOUNT_MOUNTED) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* Perform the write operation */
    *status = DISK_IO(DISK_OP_WRITE, vol_idx, daddr, buffer, count);
}
