/*
 * TIME_$GET_EC - Get event count for timer
 *
 * Returns an event count that can be waited on for clock ticks.
 * Two types are available:
 *   - ec_id 0: Normal clock EC (advances with TIME_$CLOCKH)
 *   - ec_id 1: Fast clock EC (advances more frequently)
 *
 * Parameters:
 *   ec_id - Pointer to EC type (0 or 1)
 *   ec_ret - Pointer to receive EC pointer
 *   status - Status return
 *
 * Original address: 0x00e1670a
 */

#include "time/time_internal.h"

/* Status code for bad key */
#define status_$time_bad_key 0x000D0005

/* Cached EC pointers (lazily initialized) */
static void *time_clock_ec = NULL;
static void *time_fast_clock_ec = NULL;

void TIME_$GET_EC(uint16_t *ec_id, void **ec_ret, status_$t *status)
{
    *status = status_$ok;

    /* Lazily initialize the clock ECs */
    if (time_clock_ec == NULL) {
        time_clock_ec = EC2_$REGISTER_EC1((ec_$eventcount_t *)&TIME_$CLOCKH, status);
    }

    if (time_fast_clock_ec == NULL) {
        time_fast_clock_ec = EC2_$REGISTER_EC1((ec_$eventcount_t *)&TIME_$FAST_CLOCK_EC, status);
    }

    if (*status != status_$ok) {
        return;
    }

    /* Return the requested EC */
    switch (*ec_id) {
        case 0:
            *ec_ret = time_clock_ec;
            break;
        case 1:
            *ec_ret = time_fast_clock_ec;
            break;
        default:
            *status = status_$time_bad_key;
            break;
    }
}
