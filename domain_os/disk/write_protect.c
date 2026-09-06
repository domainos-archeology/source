/*
 * DISK_$WRITE_PROTECT - Set or check write protection
 *
 * Controls write protection for a volume:
 *   mode 0: Enable write protection
 *   mode 1: Check if write protected (returns error if so)
 *
 * @param mode    0 = enable protection, 1 = check protection
 * @param vol_idx Volume index
 * @param status  Output: Status code
 */

#include "disk/disk_internal.h"

/* disk_$volume_t, DISK_VOL() and DISK_VOL_FLAG_WRITE_PROTECT come from
 * disk/disk_internal.h */

void DISK_$WRITE_PROTECT(int16_t mode, int16_t vol_idx, status_$t *status)
{
    disk_$volume_t *vol;

    *status = status_$ok;

    vol = DISK_VOL(vol_idx);

    if (mode == 0) {
        /* 0xe3d98c bset.b #0x0,(0xa5,A1) */
        vol->as_options |= DISK_VOL_FLAG_WRITE_PROTECT;
    }
    else if (mode == 1) {
        /* 0xe3d994 btst.b #0x0,(0xa5,A1) */
        if ((vol->as_options & DISK_VOL_FLAG_WRITE_PROTECT) != 0) {
            *status = status_$disk_write_protected;
        }
    }
    /* mode > 1: do nothing */
}
