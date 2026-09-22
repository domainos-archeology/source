/*
 * DISK_$MNT_DINIT - Ask a driver to initialise a unit for mounting
 *
 * 0x00E3DA64 - 0x00E3DAA0 (62 bytes).  Re-emitted from the disassembly on
 * 2026-09-19: the earlier file dropped the first argument.  The driver's
 * slot +0x08 is called with SEVEN arguments (0x00E3DA78 - 0x00E3DA96):
 * this routine's word argument, the device entry's controller word
 * (+0x06), and this routine's five longword arguments in order.
 *
 * Arguments:
 *   (0x8,A6)  unit              word by value (DISK_$PV_MOUNT_INTERNAL's
 *                               (0xe,A6), 0x00E6C374)
 *   (0xa,A6)  dev_ptr           -> VA of the disk_device_entry_t
 *   (0xe,A6)  vol_idx_ptr       passed through
 *   (0x12,A6) num_blocks_ptr    passed through
 *   (0x16,A6) sec_per_track_ptr passed through
 *   (0x1a,A6) num_heads_ptr     passed through
 *   (0x1e,A6) pvlabel_info      passed through
 * (the five longwords are the pointers DISK_$PV_MOUNT_INTERNAL was itself
 * given at (0x14..0x24,A6), 0x00E6C35C - 0x00E6C36C)
 */

#include "disk/disk_internal.h"

void DISK_$MNT_DINIT(uint16_t unit, void **dev_ptr, void *vol_idx_ptr,
                     void *num_blocks_ptr, void *sec_per_track_ptr,
                     void *num_heads_ptr, void *pvlabel_info)
{
    disk_device_entry_t *dev = (disk_device_entry_t *)*dev_ptr;     /* A1 */
    disk_jump_table_t *jt = (disk_jump_table_t *)dev->jump_table;   /* A0 */

    /* 0x00E3DA74 - 0x00E3DA96 */
    jt->dinit(unit, dev->controller, vol_idx_ptr, num_blocks_ptr,
              sec_per_track_ptr, num_heads_ptr, pvlabel_info);
}
