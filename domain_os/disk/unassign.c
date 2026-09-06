/*
 * DISK_$UNASSIGN - Unassign a volume
 *
 * Unassigns (dismounts) a volume if the current process owns it.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param status       Output: Status code
 */

#include "disk/disk_internal.h"
#include "network/network.h"

/* disk_$volume_t, DISK_VOL(), VALID_VOL_MASK and DISK_MOUNT_ASSIGNED come
 * from disk/disk_internal.h */

void DISK_$UNASSIGN(uint16_t *vol_idx_ptr, status_$t *status)
{
    uint16_t vol_idx;
    disk_$volume_t *vol;
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
        vol = DISK_VOL(vol_idx);
        mount_state = vol->mount_state;
        mount_proc = vol->mount_proc;

        if (mount_state == DISK_MOUNT_ASSIGNED && mount_proc == PROC1_$CURRENT) {
            /* Dismount the volume */
            DISK_$DISMOUNT(vol_idx);
            *status = status_$ok;
            return;
        }
    }

    *status = status_$volume_not_properly_mounted;
}
