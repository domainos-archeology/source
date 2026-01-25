/*
 * DISK_$UNASSIGN_ALL - Unassign all volumes
 *
 * Iterates through volumes 1-10 and unassigns each one.
 */

#include "disk_internal.h"

void DISK_$UNASSIGN_ALL(void)
{
    int16_t i;
    uint16_t vol_idx;
    status_$t status;

    /* Unassign volumes 1-10 */
    for (i = 9, vol_idx = 1; i >= 0; i--, vol_idx++) {
        DISK_$UNASSIGN(&vol_idx, &status);
    }
}
