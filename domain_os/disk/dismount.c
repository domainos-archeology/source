/*
 * DISK_$DISMOUNT - Dismount a volume
 *
 * Dismounts a volume by:
 * 1. Invalidating the buffer cache
 * 2. Clearing the mount state
 * 3. Shutting down the device if no other volumes use it
 *
 * @param vol_idx  Volume index
 */

#include "disk/disk_internal.h"
#include "misc/misc.h"

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* disk_$volume_t and DISK_VOL() come from disk/disk_internal.h.  Note that
 * the field this function matches on is dev_unit (+0x98, -0x2c), NOT the
 * unit_id at +0x9a that DISK_$GET_MNT_INFO reports. */

void DISK_$DISMOUNT(uint16_t vol_idx)
{
    disk_$volume_t *vol;
    int16_t mount_state;
    void *dev_info;
    int16_t unit_num;
    int16_t i;
    int16_t count;
    int16_t first_vol;
    int16_t vol_to_check;
    disk_$volume_t *entry;

    /* Validate volume index */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        return;
    }

    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /* Invalidate buffer cache for this volume */
    DISK_$INVALIDATE(vol_idx);

    /* Get volume entry */
    vol = DISK_VOL(vol_idx);

    mount_state = (int16_t)vol->mount_state;

    /* Check if volume is mounted */
    if (mount_state == DISK_MOUNT_MOUNTED || mount_state == 2) {
        /* Clear mount state if LV data exists */
        if (vol->lv_start != 0) {
            vol->mount_state = 0;
        }

        /* Count other volumes using same device */
        dev_info = vol->dev_info;
        unit_num = (int16_t)vol->dev_unit;

        first_vol = 0;
        count = 0;

        entry = DISK_VOL(1);  /* Start at volume 1 */
        for (i = 9, vol_to_check = 1; i >= 0; i--, vol_to_check++) {
            int16_t state = (int16_t)entry->mount_state;
            if ((state == 2 || state == DISK_MOUNT_MOUNTED) &&
                entry->dev_info == dev_info &&
                (int16_t)entry->dev_unit == unit_num) {

                if (entry->lv_start != 0) {
                    count++;
                } else {
                    first_vol = vol_to_check;
                }
            }
            entry++;
        }

        /* If no other volumes use this device, shut it down */
        if (count == 0) {
            if (first_vol == 0) {
                CRASH_SYSTEM(&Disk_Driver_Logic_Err);
            }

            /* Get first volume's entry and shut its device down
             * (0xe6d0b8-0xe6d0d8) */
            vol = DISK_VOL(first_vol);

            DISK_$SHUTDOWN((disk_device_entry_t *)vol->dev_info, vol->dev_unit);

            /* 0xe6d0e0-0xe6d116: walk the partition table (+0xb2) from entry 1
             * for num_parts entries, invalidating and unmounting each. */
            int16_t unit_count = (int16_t)vol->num_parts - 1;
            if (unit_count >= 0) {
                int16_t part = 1;
                for (i = unit_count; i >= 0; i--) {
                    int16_t sub_vol = (int16_t)vol->part_volx[part];
                    DISK_$INVALIDATE(sub_vol);

                    /* Clear mount state (0xe6d110 clr.w (-0x34,A3,D0*0x1)) */
                    DISK_VOL(sub_vol)->mount_state = 0;

                    part++;
                }
            }
        }
    }

    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
}
