/*
 * DISK_$READ - Read one page from a mounted (or reserved-by-me) volume
 *
 * 0x00E3CF64 - 0x00E3CFCA (104 bytes, A5 = DISK_$DATA at 0xE7A1CC).
 * Verified against the disassembly on 2026-09-19; the earlier emission was
 * faithful, this one uses the header's mount-state names.
 *
 * Arguments:
 *   (0x8,A6)  vol_idx  word (D2)
 *   (0xa,A6)  daddr    longword
 *   (0xe,A6)  ppn      longword
 *   (0x12,A6) info     -> eight longwords
 *   (0x16,A6) status   -> status_$t (A2)
 *
 * mount_state (+0x90) 3 (busy/mounted) always passes; 1 (reserved) passes
 * only when mount_proc (+0x92) is the calling process; anything else is
 * "volume not properly mounted".  DISK_IO is called with op 0
 * (0x00E3CFAC - 0x00E3CFC0: info, daddr, ppn, vol_idx, `clr.w`).
 */

#include "disk/disk_internal.h"
#include "proc1/proc1.h"

/* 0x00E3CFBA: `clr.w -(SP)` - DISK_IO's operation code for a plain read */
#define DISK_READ_OP   0

void DISK_$READ(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                status_$t *status)
{
    disk_$volume_t *vol = DISK_VOL(vol_idx);                /* 0x00E3CF7A */
    uint16_t state = vol->mount_state;                      /* 0x00E3CF88 */

    if (state == DISK_MOUNT_BUSY ||
        (state == DISK_MOUNT_RESERVED && (uint16_t)vol->mount_proc == PROC1_$CURRENT)) {
        *status = DISK_IO(DISK_READ_OP, (uint16_t)vol_idx, ppn, daddr, info);
        return;
    }

    /* 0x00E3CFA4 */
    *status = status_$volume_not_properly_mounted;
}
