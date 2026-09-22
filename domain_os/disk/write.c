/*
 * DISK_$WRITE - Write one page to a mounted volume
 *
 * 0x00E3CC78 - 0x00E3CCCC (86 bytes, A5 = DISK_$DATA at 0xE7A1CC).
 * Verified against the disassembly on 2026-09-19; the earlier emission was
 * faithful.
 *
 * Arguments:
 *   (0x8,A6)  vol_idx  word (D2)
 *   (0xa,A6)  daddr    longword
 *   (0xe,A6)  ppn      longword
 *   (0x12,A6) info     -> eight longwords
 *   (0x16,A6) status   -> status_$t (A2)
 *
 * Only mount_state 3 passes (0x00E3CC9C); DISK_IO is then called with
 * op 1 (0x00E3CCAC - 0x00E3CCC2: info, daddr, ppn, vol_idx, `#0x1`).
 */

#include "disk/disk_internal.h"

/* 0x00E3CCBA: `move.w #0x1,-(SP)` - DISK_IO's operation code for a write */
#define DISK_WRITE_OP   1

void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status)
{
    disk_$volume_t *vol = DISK_VOL(vol_idx);                /* 0x00E3CC8E */

    if (vol->mount_state != DISK_MOUNT_BUSY) {
        *status = status_$volume_not_properly_mounted;      /* 0x00E3CCA4 */
        return;
    }

    *status = DISK_IO(DISK_WRITE_OP, (uint16_t)vol_idx, ppn, daddr, info);
}
