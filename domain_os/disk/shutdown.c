/*
 * DISK_$SHUTDOWN - Shut a drive down through its driver
 *
 * 0x00E3DC28 - 0x00E3DC50 (42 bytes).  Verified against the disassembly on
 * 2026-09-19; the earlier emission was faithful.
 *
 * Arguments:
 *   (0x8,A6) dev_info  -> disk_device_entry_t (A2)
 *   (0xc,A6) unit      word by value
 *
 * The driver's slot +0x04 is called as shutdown(controller, unit) - the
 * entry's controller word (+0x06) and this routine's unit
 * (0x00E3DC3C - 0x00E3DC46) - and skipped when the slot is empty.
 */

#include "disk/disk_internal.h"

void DISK_$SHUTDOWN(disk_device_entry_t *dev_info, uint16_t unit)
{
    disk_jump_table_t *jt = (disk_jump_table_t *)dev_info->jump_table;  /* 0x00E3DC34 */

    if (jt->shutdown != NULL) {                                         /* 0x00E3DC36 */
        jt->shutdown(dev_info->controller, unit);
    }
}
