/*
 * route_$wire_routing_area - Wire memory for routing operations
 *
 * This helper function ensures that the routing code memory pages are
 * wired (locked in physical memory) before routing operations begin.
 * It only wires if not already wired.
 *
 * The function uses MST_$WIRE_AREA to wire a range of memory from
 * RTWIRED_PROC_START to RTWIRED_PROC_END, storing the wired page
 * addresses in ROUTE_$WIRED_PAGES.
 *
 * Original address: 0x00E69BCE
 * Size: 46 bytes
 */

#include "route/route_internal.h"
#include "mst/mst.h"

/*
 * Constants for routing memory wiring
 *
 * These are embedded in the code segment as PC-relative data.
 */
#if defined(M68K)
    /*
     * Maximum number of pages to wire for routing
     * Original at: 0xE69BFC (value = 10)
     */
    #define ROUTE_$MAX_WIRED_PAGES  10

    /*
     * Start address of routing code to wire
     * Original pointer at: 0xE69C04 (value = 0xE87000)
     */
    #define RTWIRED_PROC_START      0x00E87000

    /*
     * End address of routing code to wire
     * Original pointer at: 0xE69C00 (value = 0xE88228)
     */
    #define RTWIRED_PROC_END        0x00E88228
#else
    #define ROUTE_$MAX_WIRED_PAGES  10
    extern void *RTWIRED_PROC_START;
    extern void *RTWIRED_PROC_END;
#endif

/*
 * route_$wire_routing_area - Wire routing memory area if not already wired
 *
 * Called when initializing routing to ensure routing code pages are
 * wired in memory. This prevents page faults during critical routing
 * operations.
 *
 * Original address: 0x00E69BCE
 */
void route_$wire_routing_area(void)
{
    /*
     * Only wire if not already wired (N_WIRED_PAGES == 0)
     */
    if (ROUTE_$N_WIRED_PAGES == 0) {
        /*
         * MST_$WIRE_AREA parameters:
         *   1. Start address pointer
         *   2. End address pointer
         *   3. Output array for wired page addresses
         *   4. Maximum pages to wire
         *   5. Output: actual number of pages wired
         */
        MST_$WIRE_AREA(
            (void *)RTWIRED_PROC_START,
            (void *)RTWIRED_PROC_END,
            ROUTE_$WIRED_PAGES,
            ROUTE_$MAX_WIRED_PAGES,
            &ROUTE_$N_WIRED_PAGES
        );
    }
}
