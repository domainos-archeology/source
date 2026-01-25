/*
 * DISK_$GET_DRTE - Get device registration table entry
 *
 * Returns a pointer to the device registration table entry
 * for the specified index.
 *
 * @param index  Device index (0-31)
 * @return Pointer to device entry, or NULL if index invalid
 */

#include "disk_internal.h"

/* Device registration table at 0xe7ad5c */
#define DISK_DEVICE_TABLE  ((uint8_t *)0x00e7ad5c)

void *DISK_$GET_DRTE(int16_t index)
{
    /* Each entry is 12 bytes */
    return (void *)(DISK_DEVICE_TABLE + (index * DISK_DEVICE_SIZE));
}
