/*
 * DISK_$PV_MOUNT - Mount a physical volume with the default geometry
 *
 * 0x00E6C9E8 - 0x00E6CA38 (82 bytes).  Verified against the disassembly on
 * 2026-09-19.  No result slot is opened before the `bsr` at 0x00E6CA2E,
 * so D0 on return is simply whatever DISK_$PV_MOUNT_INTERNAL left there;
 * VOLX_$MOUNT / VOLX_$DISMOUNT read that D0 as the volume index, which is
 * why the C keeps the int16_t return and passes the callee's value
 * through.  The prologue loads A5 = 0xE826C4 but never uses it.
 *
 * Arguments (three words by value, then a pointer):
 *   (0x8,A6) unit_type   -> DISK_$PV_MOUNT_INTERNAL's second word
 *   (0xa,A6) device      -> its third
 *   (0xc,A6) unit        -> its fourth
 *   (0xe,A6) status      passed through
 *
 * Locals handed over with mount type 2 (0x00E6C9F4 - 0x00E6CA2A):
 * num_blocks (-0x14) = 0x12, sec_per_track (-0x1a) = 1, num_heads
 * (-0x18) = 1, vol_idx (-0x16) and the 16-byte label record (-0x10)
 * uninitialised.
 */

#include "disk/disk_internal.h"

/* 0x00E6CA2A: `move.w #0x2,-(SP)` */
#define DISK_PV_MOUNT_TYPE_MOUNT     2
/* 0x00E6C9F4: `moveq #0x12,D0` */
#define DISK_PV_MOUNT_DEFAULT_BLOCKS 0x12

int16_t DISK_$PV_MOUNT(int16_t unit_type, int16_t device, int16_t unit,
                       status_$t *status)
{
    uint16_t vol_idx = 0;                   /* (-0x16,A6): not initialised */
    uint32_t num_blocks;                    /* (-0x14,A6) */
    uint16_t sec_per_track;                 /* (-0x1a,A6) */
    uint16_t num_heads;                     /* (-0x18,A6) */
    uint32_t label[4] = {0};                /* (-0x10,A6): not initialised */

    num_blocks = DISK_PV_MOUNT_DEFAULT_BLOCKS;
    sec_per_track = 1;
    num_heads = 1;

    return DISK_$PV_MOUNT_INTERNAL(DISK_PV_MOUNT_TYPE_MOUNT, unit_type,
                                   (uint16_t)device, (uint16_t)unit,
                                   &vol_idx, &num_blocks, &sec_per_track,
                                   &num_heads, label, status);
}
