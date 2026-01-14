/*
 * PROC1_$GET_LOADAV - Get system load averages
 * Original address: 0x00e14bba
 *
 * Returns the three load average values (1, 5, and 15 minute averages).
 * These are stored as fixed-point numbers.
 *
 * Parameters:
 *   loadav - Pointer to receive 3 uint32_t load averages
 */

#include "proc1.h"

void PROC1_$GET_LOADAV(uint32_t *loadav)
{
    loadav[0] = LOADAV_1MIN;
    loadav[1] = LOADAV_5MIN;
    loadav[2] = LOADAV_15MIN;
}
