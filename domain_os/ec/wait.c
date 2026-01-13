/*
 * EC_$WAIT - Wait for eventcount(s) to reach value(s)
 *
 * Waits on up to 3 eventcounts. The ecs array is NULL-terminated
 * if fewer than 3 eventcounts are being waited on.
 *
 * Parameters:
 *   ecs - Array of up to 3 eventcount pointers (NULL-terminated)
 *   wait_val - Pointer to wait value
 *
 * Returns:
 *   Index of satisfied eventcount minus 1
 *
 * Original address: 0x00e20610
 */

#include "ec.h"

int16_t EC_$WAIT(ec_$eventcount_t *ecs[3], int32_t *wait_val)
{
    int16_t num_ecs;
    int16_t result;

    /* Count number of eventcounts (1, 2, or 3) */
    num_ecs = 1;
    if (ecs[1] != NULL) {
        num_ecs = 2;
        if (ecs[2] != NULL) {
            num_ecs = 3;
        }
    }

    /* Call the N-wait function */
    result = PROC1_$EC_WAITN(PROC1_$CURRENT_PCB, ecs, &wait_val, num_ecs);

    return result - 1;
}
