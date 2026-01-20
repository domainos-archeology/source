/*
 * tpad/set_unit.c - TPAD_$SET_UNIT implementation
 *
 * Associates the pointing device with a display unit.
 *
 * Original address: 0x00E699DC
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$SET_UNIT - Set current display unit
 *
 * Validates the unit number and sets it as the current active unit
 * for pointing device operations.
 *
 * Parameters:
 *   unitnum - Pointer to unit number (1-indexed)
 */
void TPAD_$SET_UNIT(int16_t *unitnum)
{
    int16_t ndevices;

    /* Validate unit number is positive */
    if (*unitnum <= 0) {
        return;
    }

    /* Check unit doesn't exceed number of devices */
    ndevices = SMD_$N_DEVICES();
    if (*unitnum > ndevices) {
        return;
    }

    /* Set the current unit */
    tpad_$unit = *unitnum;
}
