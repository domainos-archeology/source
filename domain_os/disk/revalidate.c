/*
 * DISK_$REVALIDATE - Revalidate a volume after media change
 *
 * Calls DISK_$REVALID with the volume's device info pointer.
 *
 * @param vol_idx  Volume index
 */

#include "disk_internal.h"

/* Volume entry offset for device info pointer */
#define DISK_DEV_INFO_OFFSET  0x7c

/* Volume entry table base (0xe7a248 = 0xe7a1cc + 0x7c) */
#define DISK_VOLUME_DEV_BASE  ((uint8_t *)0x00e7a248)

void DISK_$REVALIDATE(int16_t vol_idx)
{
    int32_t offset;
    void *dev_info;

    /* Calculate offset to device info in volume entry */
    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);

    /* Get device info pointer and call DISK_$REVALID */
    dev_info = (void *)(DISK_VOLUME_DEV_BASE + offset - DISK_VOLUME_SIZE);
    DISK_$REVALID((int16_t)(uintptr_t)dev_info);
}
