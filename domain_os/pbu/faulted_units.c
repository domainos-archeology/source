/*
 * PBU_$FAULTED_UNITS - Get count of faulted PBU units
 *
 * On systems without PBU hardware, returns 0 and sets status
 * to indicate hardware not present.
 *
 * Reverse engineered from Domain/OS at address 0x00e590fa
 */

#include "pbu/pbu.h"

/*
 * PBU_$FAULTED_UNITS
 *
 * This function would normally return the number of PBU units
 * that have experienced faults. On systems without PBU hardware,
 * it returns 0 and sets the status to indicate the hardware is
 * not present.
 */
uint16_t PBU_$FAULTED_UNITS(status_$t *status_ret)
{
    *status_ret = status_$pbu_not_present;
    return 0;
}
