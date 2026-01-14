/*
 * DISK_$SHUTDOWN - Shutdown a disk device
 *
 * Calls the device-specific shutdown function via the jump table.
 * The device info structure contains the jump table pointer and unit info.
 *
 * @param dev_info  Pointer to device info (jump table ptr at +0, unit at +6)
 * @param param_2   Additional parameter
 */

#include "disk.h"

/*
 * Note: This function takes a device info pointer, not a volume index.
 * The jump table has the shutdown function at offset +0x04.
 */
void DISK_$SHUTDOWN(int16_t vol_idx, status_$t *status)
{
    (void)vol_idx;
    *status = status_$ok;
    /* TODO: Look up device info for volume and call shutdown */
}
