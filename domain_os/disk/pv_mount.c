/*
 * DISK_$PV_MOUNT - Mount a physical volume
 *
 * Wrapper around DISK_$PV_MOUNT_INTERNAL with mount_type=2 (mount)
 * and default geometry values.
 *
 * Parameters:
 *   dev    - Device number
 *   bus    - Bus number
 *   ctlr   - Controller number
 *   status - Output: status code
 *
 * Returns:
 *   Volume index assigned, or 0 on error
 *
 * Original address: 0x00e6c9e8
 * Original size: 82 bytes
 */

#include "disk/disk_internal.h"

int16_t DISK_$PV_MOUNT(int16_t dev, int16_t bus, int16_t ctlr,
                       status_$t *status)
{
    uint16_t vol_idx;
    uint32_t num_blocks;
    uint16_t sec_per_track;
    uint16_t num_heads;
    uint8_t pvlabel_info[16];

    /* Default geometry */
    num_blocks = 0x12;
    sec_per_track = 1;
    num_heads = 1;

    return DISK_$PV_MOUNT_INTERNAL(2, dev, bus, ctlr,
                                   &vol_idx, &num_blocks,
                                   &sec_per_track, &num_heads,
                                   pvlabel_info, status);
}
