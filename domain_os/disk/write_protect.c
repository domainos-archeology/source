/*
 * DISK_$WRITE_PROTECT - Set or test a volume's write-protect flag
 *
 * 0x00E3D956 - 0x00E3D9AA (86 bytes, A5 = DISK_$DATA at 0xE7A1CC).
 * Verified against the disassembly on 2026-09-19; the earlier emission was
 * faithful.
 *
 * Arguments:
 *   (0x8,A6) mode     word (D0): 0 = set the flag, 1 = report it, anything
 *                     else does nothing
 *   (0xa,A6) vol_idx  word (D1) - not range checked
 *   (0xc,A6) status   -> status_$t, cleared first (0x00E3D970)
 *
 * The flag is bit 0 of the byte at descriptor +0xa5, the low byte of
 * as_options (`bset.b #0` at 0x00E3D98C, `btst.b #0` at 0x00E3D994).
 */

#include "disk/disk_internal.h"

#define DISK_WRITE_PROTECT_SET    0
#define DISK_WRITE_PROTECT_TEST   1

void DISK_$WRITE_PROTECT(int16_t mode, int16_t vol_idx, status_$t *status)
{
    disk_$volume_t *vol;

    *status = status_$ok;
    vol = DISK_VOL(vol_idx);                                /* 0x00E3D972 */

    if (mode == DISK_WRITE_PROTECT_SET) {
        vol->as_options |= DISK_VOL_FLAG_WRITE_PROTECT;     /* 0x00E3D98C */
    } else if (mode == DISK_WRITE_PROTECT_TEST) {
        if ((vol->as_options & DISK_VOL_FLAG_WRITE_PROTECT) != 0) {
            *status = status_$disk_write_protected;         /* 0x00E3D99C */
        }
    }
}
