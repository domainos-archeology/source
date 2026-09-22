/*
 * DISK_$REVALIDATE - Revalidate a volume by index
 *
 * 0x00E6C060 - 0x00E6C08A (44 bytes).  Verified against the disassembly on
 * 2026-09-19; the earlier emission was faithful.  (The batch list gives
 * 0x00E6C06A, which is the `movea.l #0xe7a290,A0` inside this function.)
 *
 * Argument: (0x8,A6) vol_idx, word by value (D2).
 *
 * 0x00E6C06A - 0x00E6C07A: 0xE7A290 + vol_idx * 0x48 - 0x48, i.e.
 * DISK_VOLUME_BASE + 0x7c + vol_idx * 0x48 = DISK_VOL(vol_idx), handed to
 * DISK_$REVALID by reference.
 */

#include "disk/disk_internal.h"

void DISK_$REVALIDATE(int16_t vol_idx)
{
    DISK_$REVALID(DISK_VOL(vol_idx));
}
