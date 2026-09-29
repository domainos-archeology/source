/*
 * route_$wire_routing_area - Wire memory for routing operations
 *
 * This helper function ensures that the routing code memory pages are
 * wired (locked in physical memory) before routing operations begin.
 * It only wires if not already wired.
 *
 * The function uses MST_$WIRE_AREA to wire a range of memory from
 * RTWIRED_PROC_START to RTWIRED_DATA_END, storing the wired page
 * addresses in ROUTE_$RTWIRED_DATA.wired_pages (map ROUTE_$WIRED_PAGES).
 *
 * Original address: 0x00E69BCE
 * Size: 46 bytes
 */

#include "route/route_internal.h"
#include "mst/mst.h"

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
 * PC-relative constant cells between this routine and ROUTE_$SHORT_PORT
 * (`gsk read 0xE69BFC 0xC`: 00 0a 20 48 00 e8 82 28 00 e8 70 00); pointers
 * to them are passed to MST_$WIRE_AREA:
 *   0xE69BFC: word 10          (maximum pages to wire; 0xE69BFE is padding)
 *   0xE69C00: long 0x00E88228  (end of the wired routing area,
 *                               RTWIRED_DATA_END = one past ROUTE_$RTWIRED_DATA)
 *   0xE69C04: long 0x00E87000  (start of the wired routing area,
 *                               RTWIRED_PROC_START)
 *
 * The end cell names a linked object, so it is a link-time value
 * (ARCH_PTR_TO_VA_STATIC; the image value is the second argument).  The
 * start is RIP_$SEND_TO_PORT_INTERNET, a nested static of rip/send.c that
 * is not yet placed at the head of the region.
 * TODO(source-82cs): make route_$wired_area_start a link-time value.
 */
static const uint16_t route_$max_wired_pages = ROUTE_$MAX_WIRED_PAGES;
static const uint32_t route_$wired_area_end =
    ARCH_PTR_TO_VA_STATIC(&ROUTE_$RTWIRED_DATA + 1, 0x00E88228u);
static const uint32_t route_$wired_area_start = 0x00E87000u;

void route_$wire_routing_area(void)
{
    /*
     * Only wire if not already wired (N_WIRED_PAGES == 0)
     */
    if (ROUTE_$RTWIRED_DATA.n_wired_pages == 0) {
        /*
         * MST_$WIRE_AREA parameters:
         *   1. Start address pointer
         *   2. End address pointer
         *   3. Output array for wired page addresses
         *   4. Maximum pages to wire
         *   5. Output: actual number of pages wired
         */
        MST_$WIRE_AREA(
            &route_$wired_area_start,           /* pea (0x14,PC) -> 0xE69C04 */
            &route_$wired_area_end,             /* pea (0x14,PC) -> 0xE69C00 */
            ROUTE_$RTWIRED_DATA.wired_pages,    /* 0xE87D80 */
            (void *)&route_$max_wired_pages,    /* pea (0x1a,PC) -> 0xE69BFC */
            &ROUTE_$RTWIRED_DATA.n_wired_pages  /* 0xE87FD2 */
        );
    }
}
