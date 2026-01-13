/*
 * EC_$WAITN - Wait for N eventcounts
 *
 * Allows waiting on an arbitrary number of eventcounts.
 *
 * Parameters:
 *   ecs - Array of eventcount pointers
 *   wait_val - Array of wait values
 *   num_ecs - Number of eventcounts to wait on
 *
 * Returns:
 *   Index of satisfied eventcount
 *
 * Original address: 0x00e2063e
 */

#include "ec.h"

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    return PROC1_$EC_WAITN(PROC1_$CURRENT_PCB, ecs, wait_val, num_ecs);
}
