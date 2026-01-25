/*
 * DISK_$REVALID - Revalidate disk after media change
 *
 * Dispatches a revalidate request to the device-specific
 * handler via the device's jump table.
 *
 * @param req  Request block containing device info
 */

#include "disk_internal.h"

void DISK_$REVALID(int16_t vol_idx)
{
    /* Note: This function signature in header doesn't match decompiled.
     * The decompiled version takes an I/O request block and calls
     * the revalidate function at offset +0x0c in the jump table.
     * This stub implements the simpler signature from the header.
     */
    (void)vol_idx;
    /* TODO: Implement full revalidation logic */
}
