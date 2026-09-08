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
    int16_t count;      /* D2, snapshotted before the loop */

    /*
     * Only cleanup if:
     *   - No user ports are active
     *   - Routing is not running
     */
    if (ROUTE_$N_USER_PORTS != 0) {
        return;
    }

    /* 0x00E69B8C: tst.b (0x00E26F1E).l / bmi - ROUTE_$ROUTING is a byte */
    if (ROUTE_$ROUTING < 0) {
        return;
    }

    /*
     * 0x00E69B94-0x00E69BBA: unwire ROUTE_$WIRED_PAGES[0 .. count-1],
     * ASCENDING.
     *
     *   00e69b94  move.w (0x00e87fd2).l,D0w   ; D0 = ROUTE_$N_WIRED_PAGES
     *   00e69b9a  subq.w #0x1,D0w             ; count - 1
     *   00e69b9c  bmi.b                       ; a count of 0 skips the loop
     *   00e69b9e  movea.l #0xe87d80,A0        ; the array base
     *   00e69ba4  move.w D0w,D2w              ; the dbf counter
     *   00e69ba6  lea (0x4,A0),A2             ; A2 = &pages[1]
     *   00e69bac  move.l (-0x4,A2),-(SP)      ; pages[i], by value
     *   00e69bb8  addq.l #0x4,A2              ; forward one element
     *   00e69bba  dbf D2w
     *
     * The cursor starts one element PAST the base and every read is at
     * -4 from it, so the first page unwired is element 0 and the walk runs
     * upward; the dbf makes the body run count times.
     */
    count = ROUTE_$N_WIRED_PAGES;
    for (i = 0; i < count; i++) {
        WP_$UNWIRE(ROUTE_$WIRED_PAGES[i]);
    }

    /* 0x00E69BBE: reset the wired page count */
    ROUTE_$N_WIRED_PAGES = 0;
}
