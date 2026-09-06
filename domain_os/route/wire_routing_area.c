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
 * Constants for routing memory wiring (ROUTE_$MAX_WIRED_PAGES,
 * ROUTE_$WIRED_AREA_START/END) are embedded in the code segment as
 * PC-relative data in the original; see route/route_internal.h.
 */

/*
 * route_$wire_routing_area - Wire routing memory area if not already wired
 *
 * Called when initializing routing to ensure routing code pages are
 * wired in memory. This prevents page faults during critical routing
 * operations.
 *
 * Original address: 0x00E69BCE
 */
/*
 * PC-relative constants of the original (pointers to these are passed to
 * MST_$WIRE_AREA):
 *   0xE69BFC: word 10          (maximum pages to wire)
 *   0xE69C00: long 0x00E88228  (end of the wired routing area)
 *   0xE69C04: long 0x00E87000  (start of the wired routing area)
 */
static const uint16_t route_$max_wired_pages = ROUTE_$MAX_WIRED_PAGES;
static void *const route_$wired_area_end = ROUTE_$WIRED_AREA_END;
static void *const route_$wired_area_start = ROUTE_$WIRED_AREA_START;

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
            (void *)&route_$wired_area_start,   /* pea (0x14,PC) -> 0xE69C04 */
            (void *)&route_$wired_area_end,     /* pea (0x14,PC) -> 0xE69C00 */
            ROUTE_$WIRED_PAGES,                 /* 0xE87D80 */
            (void *)&route_$max_wired_pages,    /* pea (0x1a,PC) -> 0xE69BFC */
            &ROUTE_$N_WIRED_PAGES               /* 0xE87FD2 */
        );
    }
}
