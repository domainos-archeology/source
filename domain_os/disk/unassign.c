/*
 * DISK_$UNASSIGN - Unassign a volume
 *
 * Unassigns (dismounts) a volume if the current process owns it.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param status       Output: Status code
 */

#include "disk.h"

/* Diskless flag */
extern int8_t NETWORK_$REALLY_DISKLESS;

/* Current process ID */
extern int16_t PROC1_$CURRENT;

/* Mount state and process at offset 0x90 and 0x92 in volume entry */
#define DISK_MOUNT_STATE_OFFSET  0x90
#define DISK_MOUNT_PROC_OFFSET   0x92

/* Volume entry table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Mount state 2 = assigned */
#define DISK_MOUNT_ASSIGNED  2

/* Forward declaration */
extern void DISK_$DISMOUNT(uint16_t vol_idx);

void DISK_$UNASSIGN(uint16_t *vol_idx_ptr, status_$t *status)
{
    uint16_t vol_idx;
    int32_t offset;
    uint16_t mount_state;
    int16_t mount_proc;

    vol_idx = *vol_idx_ptr;

    /* Check if diskless */
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        /* Validate volume index (must be 1-10) */
        if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
            *status = status_$invalid_volume_index;
            return;
        }

        /* Check mount state and ownership */
        offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
        mount_state = *(uint16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_STATE_OFFSET);
        mount_proc = *(int16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_PROC_OFFSET);

        if (mount_state == DISK_MOUNT_ASSIGNED && mount_proc == PROC1_$CURRENT) {
            /* Dismount the volume */
            DISK_$DISMOUNT(vol_idx);
            *status = status_$ok;
            return;
        }
    }

    *status = status_$volume_not_properly_mounted;
}
