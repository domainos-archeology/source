/*
 * DISK_$AS_OPTIONS - Set the async I/O option word of an assigned volume
 *
 * 0x00E6C0A8 - 0x00E6C114 (110 bytes).  Verified against the disassembly
 * on 2026-09-08 (the earlier emission was already faithful).
 *
 * Arguments:
 *   (0x8,A6)  vol_idx_ptr  -> word volume index
 *   (0xc,A6)  options_ptr  -> word option value
 *   (0x10,A6) status       -> status_$t
 */

#include "disk/disk_internal.h"
#include "proc1/proc1.h"

void DISK_$AS_OPTIONS(uint16_t *vol_idx_ptr, uint16_t *options_ptr, status_$t *status)
{
    uint16_t vol_idx;
    uint16_t options;
    disk_$volume_t *vol;

    /* 0x00E6C0BC - 0x00E6C0BE: both words are read before any check */
    vol_idx = *vol_idx_ptr;
    options = *options_ptr;

    /* 0x00E6C0C0 - 0x00E6C0D4: `btst.l D2,D3` against 0x7fe, bit number
     * modulo 32 */
    if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    /* 0x00E6C0D6 */
    *status = status_$ok;

    /* 0x00E6C0D8 - 0x00E6C0E8: 0xE7A290 + vol_idx * 0x48, word arithmetic */
    vol = DISK_VOL(vol_idx);

    /* 0x00E6C0EC - 0x00E6C106: must be assigned to the calling process */
    if (vol->mount_state != DISK_MOUNT_ASSIGNED ||
        (uint16_t)vol->mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return;
    }

    /* 0x00E6C108: `move.w D0w,(-0x20,A0)` - a whole-word store, so it also
     * overwrites the DISK_VOL_FLAG_* byte in the low half. */
    vol->as_options = options;
}
