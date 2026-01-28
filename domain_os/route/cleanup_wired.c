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
 * Additional globals for wired page tracking
 *
 * These are defined in the data segment near the routing code and
 * track pages wired for routing operations.
 */
#if defined(ARCH_M68K)
    /*
     * ROUTE_$WIRED_PAGES - Array of wired page addresses
     *
     * Holds up to N page addresses that have been wired for routing.
     *
     * Original address: 0xE87D80
     */
    #define ROUTE_$WIRED_PAGES      ((uint32_t *)0xE87D80)

    /*
     * ROUTE_$N_WIRED_PAGES - Count of currently wired pages
     *
     * Original address: 0xE87FD2
     */
    #define ROUTE_$N_WIRED_PAGES    (*(int16_t *)0xE87FD2)

    /*
     * ROUTE_$N_USER_PORTS - Count of active user ports
     *
     * Original address: 0xE87FD4
     */
    #define ROUTE_$N_USER_PORTS     (*(int16_t *)0xE87FD4)
#else
    extern uint32_t ROUTE_$WIRED_PAGES[];
    extern int16_t ROUTE_$N_WIRED_PAGES;
    extern int16_t ROUTE_$N_USER_PORTS;
#endif

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
