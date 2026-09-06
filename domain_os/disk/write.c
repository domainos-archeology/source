/*
 * DISK_$WRITE - Write data to disk
 *
 * Writes data to a mounted volume. Validates that the volume
 * is properly mounted before performing the I/O.
 *
 * @param vol_idx  Volume index
 * @param daddr    Disk address (arg 2, (0xa,A6))
 * @param ppn      Physical page number of the transfer buffer (arg 3, (0xe,A6))
 * @param info     8-longword block header (arg 4, (0x12,A6))
 * @param status   Output: Status code
 */

#include "disk/disk_internal.h"

/* disk_$volume_t and DISK_VOL() come from disk_internal.h */

/* I/O operation codes */
#define DISK_OP_WRITE  1

void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status)
{
    uint16_t mount_state;

    /* Get mount state */
    mount_state = DISK_VOL(vol_idx)->mount_state;

    if (mount_state != DISK_MOUNT_MOUNTED) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* Perform the write operation */
    *status = DISK_IO(DISK_OP_WRITE, vol_idx, ppn, daddr, info);
}
