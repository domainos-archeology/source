/*
 * DISK_$AS_OPTIONS - Set async I/O options for a volume
 *
 * Sets async I/O options for an assigned volume.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param options_ptr  Pointer to options value
 * @param status       Output: Status code
 */

#include "disk/disk_internal.h"

/* VALID_VOL_MASK, DISK_MOUNT_ASSIGNED and disk_$volume_t come from
 * disk/disk_internal.h */

void DISK_$AS_OPTIONS(uint16_t *vol_idx_ptr, uint16_t *options_ptr, status_$t *status)
{
    uint16_t vol_idx;
    uint16_t options;
    disk_$volume_t *vol;
    uint16_t mount_state;
    int16_t mount_proc;

    vol_idx = *vol_idx_ptr;
    options = *options_ptr;

    /* Validate volume index (must be 1-10) */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    *status = status_$ok;

    vol = DISK_VOL(vol_idx);

    /* Check mount state and ownership (0xe6c0ec / 0xe6c0f4) */
    mount_state = vol->mount_state;
    mount_proc = vol->mount_proc;

    if (mount_state != DISK_MOUNT_ASSIGNED || mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* Set the async options (0xe6c108 move.w D0w,(-0x20,A0)).  This is a
     * whole-word store, so it also overwrites the DISK_VOL_FLAG_* byte. */
    vol->as_options = options;
}
