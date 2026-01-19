/*
 * RIP_$AGE - Age routing table entries
 *
 * This function is called periodically to age routing table entries.
 * Routes transition through states based on their expiration times:
 *
 *   VALID -> AGING -> EXPIRED -> UNUSED
 *
 * After aging, routing updates are sent to propagate changes.
 *
 * Original address: 0x00E155C0
 */

#include "rip/rip_internal.h"

/*
 * RIP_$AGE - Age routing table entries
 *
 * Algorithm:
 * 1. Acquire RIP lock
 * 2. For each entry in the routing table (64 entries):
 *    For each route slot (2 per entry: standard and non-standard):
 *      - If state is non-zero (not unused) and time has expired:
 *        - State 1 (VALID): If metric != 0, transition to AGING
 *        - State 2 (AGING): Set metric to infinity (0x11), mark recent changes,
 *                           transition to EXPIRED
 *        - State 3 (EXPIRED): Transition to UNUSED
 * 3. Release RIP lock
 * 4. Send routing updates for both standard and non-standard routes
 *
 * The timeout period is RIP_ROUTE_TIMEOUT (0x168 = 360 ticks, approximately 6 minutes).
 */
void RIP_$AGE(void)
{
    int entry_idx;          /* Current entry index (0-63) */
    int route_idx;          /* Current route index (0=standard, 1=non-standard) */
    rip_$entry_t *entry;    /* Current entry pointer */
    rip_$route_t *route;    /* Current route pointer */
    uint8_t state;          /* Route state (top 2 bits of flags) */
    uint32_t current_time;  /* Current clock value */

    /* Acquire RIP lock */
    RIP_$LOCK();

    /* Get current time once for consistency */
    current_time = TIME_$CLOCKH;

    /* Iterate through all entries */
    entry = &RIP_$DATA.entries[0];
    for (entry_idx = RIP_TABLE_SIZE - 1; entry_idx >= 0; entry_idx--) {

        /* Iterate through both route slots */
        for (route_idx = 0; route_idx < RIP_ROUTES_PER_ENTRY; route_idx++) {
            /* Get route pointer (slot 0 at offset 0x04, slot 1 at offset 0x18) */
            route = &entry->routes[route_idx];

            /* Get state from top 2 bits of flags */
            state = (route->flags >> RIP_STATE_SHIFT) & 0x03;

            /* Skip unused routes */
            if (state == RIP_STATE_UNUSED) {
                continue;
            }

            /* Check if route has expired */
            if ((int32_t)current_time <= (int32_t)route->expiration) {
                continue;  /* Not expired yet */
            }

            /* Handle based on current state */
            switch (state) {
            case RIP_STATE_VALID:
                /*
                 * Valid route expired - transition to AGING if metric is non-zero.
                 * Routes with metric 0 are direct routes that shouldn't age.
                 */
                if (route->metric != 0) {
                    /* Set new expiration time */
                    route->expiration = current_time + RIP_ROUTE_TIMEOUT;
                    /* Transition to AGING state */
                    route->flags = (route->flags & ~RIP_STATE_MASK) |
                                   (RIP_STATE_AGING << RIP_STATE_SHIFT);
                }
                break;

            case RIP_STATE_AGING:
                /*
                 * Aging route expired - transition to EXPIRED.
                 * Set metric to infinity and mark that we have changes to advertise.
                 */
                route->metric = RIP_INFINITY;

                /* Mark recent changes flag based on route type */
                if (route_idx == 1) {
                    /* Non-standard route (index 1) */
                    RIP_$DATA.std_recent_changes = 0xFF;
                } else {
                    /* Standard route (index 0) */
                    RIP_$DATA.recent_changes = 0xFF;
                }

                /* Set new expiration time */
                route->expiration = current_time + RIP_ROUTE_TIMEOUT;
                /* Transition to EXPIRED state */
                route->flags |= RIP_STATE_MASK;  /* State 3 = 0xC0 */
                break;

            case RIP_STATE_EXPIRED:
                /*
                 * Expired route expired again - clear the entry.
                 * Set state to UNUSED so the slot can be reused.
                 */
                route->flags &= ~RIP_STATE_MASK;  /* State 0 = unused */
                break;
            }
        }

        entry++;  /* Move to next entry */
    }

    /* Release RIP lock */
    RIP_$UNLOCK();

    /*
     * Send routing updates for any changes.
     * Call twice: once for standard routes (0), once for non-standard (0xFF).
     *
     * Note: The assembly shows pushing 0 and 0xFF (as -1 sign-extended to word),
     * which corresponds to the two route types.
     */
    RIP_$SEND_UPDATES(0);       /* Standard routes */
    RIP_$SEND_UPDATES(0xFF);    /* Non-standard routes */
}
