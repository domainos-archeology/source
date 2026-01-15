/*
 * FLP_$SHUTDOWN - Shutdown a floppy unit
 *
 * This function marks a floppy unit as inactive and returns
 * the count of remaining active units. This is used during
 * system shutdown or when removing a drive from service.
 */

#include "flp/flp_internal.h"

/*
 * FLP_$SHUTDOWN - Shutdown floppy unit
 *
 * @param unit  Unit number (0-3)
 * @return Number of remaining active units
 */
int16_t FLP_$SHUTDOWN(uint16_t unit)
{
    int16_t active_count;
    int16_t i;

    /* Mark this unit as inactive */
    DAT_00e7b014[unit] = 0;

    /* Count remaining active units */
    active_count = 0;
    for (i = 0; i < FLP_MAX_UNITS; i++) {
        /* High bit set indicates unit is active */
        if ((int8_t)DAT_00e7b014[i] < 0) {
            active_count++;
        }
    }

    return active_count;
}
