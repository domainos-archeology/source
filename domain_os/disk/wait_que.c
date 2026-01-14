/*
 * DISK_$WAIT_QUE - Wait for queue completion
 *
 * Waits for I/O operations on a queue to complete.
 * This is a wrapper for an internal wait function.
 *
 * @param mask     Bitmask of volumes to wait for
 * @param param_2  I/O counter pointer
 * @param param_3  Completion counter pointer
 */

#include "disk/disk_internal.h"

void DISK_$WAIT_QUE(void *queue, status_$t *status)
{
    (void)queue;
    (void)status;
    /* Note: Actual signature is different - takes mask and two counter pointers.
     * The header signature needs updating to match.
     * FUN_00e3c9fe(mask, counter1, counter2);
     */
}
