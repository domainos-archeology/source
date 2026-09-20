/*
 * DISK_$DO_IO - Hand a request to the volume's driver
 *
 * 0x00E3DAA2 - 0x00E3DAD2 (50 bytes).  Verified against the disassembly on
 * 2026-09-19; the earlier emission was faithful, this one goes through the
 * records.
 *
 * Arguments:
 *   (0x8,A6)  vol      the volume descriptor (the +0x7c form, see disk.h);
 *                      its dev_info (+0x18) is the disk_device_entry_t
 *                      whose jump_table (+0x00) holds the driver vector
 *   (0xc,A6)  req      passed through
 *   (0x10,A6) param_3  passed through (every caller passes `req` again)
 *   (0x14,A6) result   passed through (DISK_IO hands a byte cell,
 *                      0x00E3D71A)
 *
 * The driver's slot +0x10 is called with the same four arguments in the
 * same order (0x00E3DAB8 - 0x00E3DAC8); D0 is whatever it left there.
 */

#include "disk/disk_internal.h"

void DISK_$DO_IO(void *vol, void *req, void *param_3, void *result)
{
    disk_$volume_t *v = (disk_$volume_t *)vol;                      /* A2 */
    disk_device_entry_t *dev = (disk_device_entry_t *)v->dev_info;  /* A1 = (0x18,A2) */
    disk_jump_table_t *jt = (disk_jump_table_t *)dev->jump_table;   /* A0 = (A1) */

    /* 0x00E3DAB4 - 0x00E3DAC8 */
    jt->do_io(vol, req, param_3, result);
}
