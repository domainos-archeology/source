/*
 * DISK_$REVALIDATE - Revalidate a volume after media change
 *
 * Calls DISK_$REVALID with the volume's device info pointer.
 *
 * @param vol_idx  Volume index
 */

#include "disk/disk_internal.h"

/* disk_$volume_t and DISK_VOL() come from disk/disk_internal.h */

void DISK_$REVALIDATE(int16_t vol_idx)
{
    /*
     * 0x00E6C06A:
     *   movea.l #0xe7a290,A0
     *   move.w  D2w,D0w ; lsl.w #0x3,D0w
     *   move.w  D0w,D1w ; lsl.w #0x3,D1w ; add.w D1w,D0w   ; D0 = vol * 0x48
     *   pea     (-0x48,A0,D0w*0x1)
     * 0xE7A290 - 0x48 = 0xE7A248 = DISK_VOLUME_BASE + DISK_VOL_DESC_OFFSET,
     * so the address handed over is DISK_VOL(vol_idx) -- the descriptor of
     * this volume, not of the one before it.
     */
    DISK_$REVALID(DISK_VOL(vol_idx));
}
