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

/* Volume table offsets */
#define DISK_MOUNT_STATE_OFFSET  0x90
#define DISK_MOUNT_PROC_OFFSET   0x92
#define DISK_AS_OPTIONS_OFFSET   0xa4  /* Async options offset */

/* Volume table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Mount state 2 = assigned */
#define DISK_MOUNT_ASSIGNED  2

void DISK_$AS_OPTIONS(uint16_t *vol_idx_ptr, uint16_t *options_ptr, status_$t *status)
{
    uint16_t vol_idx;
    uint16_t options;
    int32_t offset;
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

    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);

    /* Check mount state and ownership */
    mount_state = *(uint16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_STATE_OFFSET);
    mount_proc = *(int16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_PROC_OFFSET);

    if (mount_state != DISK_MOUNT_ASSIGNED || mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* Set the async options */
    *(uint16_t *)(DISK_VOLUME_BASE + offset + DISK_AS_OPTIONS_OFFSET) = options;
}
