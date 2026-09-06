/*
 * DISK_$SHUTDOWN - Shut a disk device down through its driver
 *
 * Fetches the device's jump table and, if it has a shutdown entry at +0x04,
 * calls it with the device's controller number and the unit number.
 *
 * Original address: 0x00e3dc28
 * Size: 42 bytes
 *
 * The original is a Pascal function whose (unused) two-byte result slot the
 * callers reserve and discard; it never assigns a result, so nothing is
 * returned here.
 */

#include "disk/disk_internal.h"

void DISK_$SHUTDOWN(disk_device_entry_t *dev_info, uint16_t unit)
{
    disk_jump_table_t *jump_table;

    jump_table = (disk_jump_table_t *)dev_info->jump_table;   /* 0xe3dc34 */

    if (jump_table->shutdown != NULL) {                        /* 0xe3dc36 */
        /* 0xe3dc3c-0xe3dc46: the driver entry takes (controller, unit) */
        jump_table->shutdown(dev_info->controller, unit);
    }
}
