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
    /* The original passes the descriptor of volume vol_idx - 1:
     * 0xe7a248 + vol*0x48 - 0x48 == DISK_VOL(vol_idx - 1). */
    DISK_$REVALID((int16_t)(uintptr_t)DISK_VOL(vol_idx - 1));
}
