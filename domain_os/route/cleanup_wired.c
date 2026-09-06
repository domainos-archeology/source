/*
 * ROUTE_$CLEANUP_WIRED - Cleanup wired pages when routing stops
 *
 * This helper function is called when routing is shutting down to unwire
 * any pages that were wired for routing operations. It only performs
 * cleanup if there are no user ports active and routing is not running.
 *
 * Original address: 0x00E69B7C
 * Size: 82 bytes
 */

#include "route/route_internal.h"
#include "wp/wp.h"

/*
 * ROUTE_$WIRED_PAGES, ROUTE_$N_WIRED_PAGES and ROUTE_$N_USER_PORTS are
 * declared in route/route_internal.h.
 */

/*
 * ROUTE_$CLEANUP_WIRED - Unwire pages when no longer needed
 *
 * Called when routing is being disabled. Unwires all pages that were
 * wired for routing operations, but only if:
 *   1. No user ports are active (ROUTE_$N_USER_PORTS == 0)
 *   2. Routing is not actively running (ROUTE_$ROUTING high bit not set)
 *
 * Original address: 0x00E69B7C
 */
void ROUTE_$CLEANUP_WIRED(void)
{
    int16_t i;

    /*
     * Only cleanup if:
     *   - No user ports are active
     *   - Routing is not running (high byte bit 7 not set)
     */
    if (ROUTE_$N_USER_PORTS != 0) {
        return;
    }

    if ((int8_t)(ROUTE_$ROUTING >> 8) < 0) {
        return;
    }

    /*
     * Iterate through all wired pages and unwire them.
     * Loop from 0 to N_WIRED_PAGES-1.
     */
    for (i = ROUTE_$N_WIRED_PAGES - 1; i >= 0; i--) {
        WP_$UNWIRE(ROUTE_$WIRED_PAGES[i]);
    }

    /* Reset wired page count */
    ROUTE_$N_WIRED_PAGES = 0;
}
