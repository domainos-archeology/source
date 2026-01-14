/*
 * DISK_$READ - Read data from disk
 *
 * Reads data from a volume. Validates that the volume is properly
 * mounted before performing the I/O. Read access is allowed if:
 * - Volume is fully mounted (state 3), OR
 * - Volume is partially mounted (state 1) by the current process
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

/* Mounting process offset within volume entry */
#define DISK_MOUNT_PROC_OFFSET   0x92

/* Volume entry table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* I/O operation codes */
#define DISK_OP_READ   0

/* Mount states */
#define DISK_MOUNT_PARTIAL  1
#define DISK_MOUNT_FULL     3

/* Current process ID */
extern int16_t PROC1__CURRENT;

/* External I/O function */
extern status_$t DISK_IO(int16_t op, int16_t vol_idx, void *daddr,
                         void *buffer, void *count);

void DISK_$READ(int16_t vol_idx, void *buffer, void *daddr, void *count,
                status_$t *status)
{
    int32_t offset;
    uint16_t mount_state;
    int16_t mount_proc;

    /* Calculate offset to volume entry */
    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);

    /* Get mount state */
    mount_state = *(uint16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_STATE_OFFSET);

    if (mount_state == DISK_MOUNT_FULL) {
        /* Fully mounted - allow read */
        *status = DISK_IO(DISK_OP_READ, vol_idx, daddr, buffer, count);
        return;
    }

    if (mount_state == DISK_MOUNT_PARTIAL) {
        /* Partially mounted - check if current process owns it */
        mount_proc = *(int16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_PROC_OFFSET);
        if (mount_proc == PROC1__CURRENT) {
            *status = DISK_IO(DISK_OP_READ, vol_idx, daddr, buffer, count);
            return;
        }
    }

    /* Volume not accessible */
    *status = status_$volume_not_properly_mounted;
}
