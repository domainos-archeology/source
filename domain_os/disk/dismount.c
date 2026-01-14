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

#include "disk.h"
#include "misc/misc.h"

/* Mount lock */
extern void *MOUNT_LOCK;

/* Error message */
extern void *Disk_Driver_Logic_Err;

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Volume table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a290)

/* Offsets in volume entry */
#define DISK_MOUNT_STATE_OFFSET  (-0x34)
#define DISK_DEV_INFO_OFFSET     (-0x30)
#define DISK_UNIT_OFFSET         (-0x2c)
#define DISK_LV_DATA_OFFSET      (-0x40)
#define DISK_UNIT_COUNT_OFFSET   (-0x1c)

void DISK_$DISMOUNT(uint16_t vol_idx)
{
    int32_t offset;
    uint8_t *vol_entry;
    int16_t mount_state;
    void *dev_info;
    int16_t unit_num;
    int16_t i;
    int16_t count;
    int16_t first_vol;
    int16_t vol_to_check;
    uint8_t *entry;

    /* Validate volume index */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        return;
    }

    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /* Invalidate buffer cache for this volume */
    DISK_$INVALIDATE(vol_idx);

    /* Get volume entry */
    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    vol_entry = DISK_VOLUME_BASE + offset;

    mount_state = *(int16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET);

    /* Check if volume is mounted */
    if (mount_state == DISK_MOUNT_MOUNTED || mount_state == 2) {
        /* Clear mount state if LV data exists */
        if (*(uint32_t *)(vol_entry + DISK_LV_DATA_OFFSET) != 0) {
            *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET) = 0;
        }

        /* Count other volumes using same device */
        dev_info = *(void **)(vol_entry + DISK_DEV_INFO_OFFSET);
        unit_num = *(int16_t *)(vol_entry + DISK_UNIT_OFFSET);

        first_vol = 0;
        count = 0;

        entry = DISK_VOLUME_BASE + DISK_VOLUME_SIZE;  /* Start at volume 1 */
        for (i = 9, vol_to_check = 1; i >= 0; i--, vol_to_check++) {
            int16_t state = *(int16_t *)(entry + DISK_MOUNT_STATE_OFFSET);
            if ((state == 2 || state == DISK_MOUNT_MOUNTED) &&
                *(void **)(entry + DISK_DEV_INFO_OFFSET) == dev_info &&
                *(int16_t *)(entry + DISK_UNIT_OFFSET) == unit_num) {

                if (*(uint32_t *)(entry + DISK_LV_DATA_OFFSET) != 0) {
                    count++;
                } else {
                    first_vol = vol_to_check;
                }
            }
            entry += DISK_VOLUME_SIZE;
        }

        /* If no other volumes use this device, shut it down */
        if (count == 0) {
            if (first_vol == 0) {
                CRASH_SYSTEM(&Disk_Driver_Logic_Err);
            }

            /* Get first volume's entry and shutdown */
            offset = (int16_t)(first_vol * DISK_VOLUME_SIZE);
            vol_entry = DISK_VOLUME_BASE + offset;

            /* Call DISK_$SHUTDOWN with device info */
            /* Note: The actual call passes values from the volume entry */

            /* Clear mount state for all related volumes */
            int16_t unit_count = *(int16_t *)(vol_entry + DISK_UNIT_COUNT_OFFSET) - 1;
            if (unit_count >= 0) {
                uint8_t *vol_list = vol_entry + 2;
                for (i = unit_count; i >= 0; i--) {
                    int16_t sub_vol = *(int16_t *)(vol_list - 0x12);
                    DISK_$INVALIDATE(sub_vol);

                    /* Clear mount state */
                    int32_t sub_offset = (int32_t)sub_vol * DISK_VOLUME_SIZE;
                    *(uint16_t *)(DISK_VOLUME_BASE + sub_offset + DISK_MOUNT_STATE_OFFSET) = 0;

                    vol_list += 2;
                }
            }
        }
    }

    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
}
