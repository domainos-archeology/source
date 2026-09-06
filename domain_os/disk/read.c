/*
 * DISK_$READ - Read data from disk
 *
 * Reads data from a volume. Validates that the volume is properly
 * mounted before performing the I/O. Read access is allowed if:
 * - Volume is fully mounted (state 3), OR
 * - Volume is partially mounted (state 1) by the current process
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
#define DISK_OP_READ   0

/* Mount states */
#define DISK_MOUNT_PARTIAL  1
#define DISK_MOUNT_FULL     3

void DISK_$READ(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                status_$t *status)
{
    disk_$volume_t *vol;
    uint16_t mount_state;
    int16_t mount_proc;

    vol = DISK_VOL(vol_idx);

    /* Get mount state */
    mount_state = vol->mount_state;

    if (mount_state == DISK_MOUNT_FULL) {
        /* Fully mounted - allow read */
        *status = DISK_IO(DISK_OP_READ, vol_idx, ppn, daddr, info);
        return;
    }

    if (mount_state == DISK_MOUNT_PARTIAL) {
        /* Partially mounted - check if current process owns it */
        mount_proc = vol->mount_proc;
        if (mount_proc == PROC1_$CURRENT) {
            *status = DISK_IO(DISK_OP_READ, vol_idx, ppn, daddr, info);
            return;
        }
    }

    /* Volume not accessible */
    *status = status_$volume_not_properly_mounted;
}
