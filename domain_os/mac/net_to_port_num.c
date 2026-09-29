/*
 * MAC_$NET_TO_PORT_NUM - Convert network ID to port number
 *
 * Looks up the port number for a given network identifier.
 * If net_id is 0, returns port 0.
 * Otherwise searches through the port table for a match.
 *
 * Original address: 0x00E0C350
 * Original size: 74 bytes
 */

#include "mac/mac_internal.h"
#include "route/route.h"

void MAC_$NET_TO_PORT_NUM(int32_t *net_id, int16_t *port_ret)
{
    int32_t network;
    int16_t i;
    int32_t port_net_id;

    network = *net_id;

    /* If network ID is 0, return port 0 */
    if (network == 0) {
        *port_ret = 0;
        return;
    }

    /* Default to -1 (not found) */
    *port_ret = -1;

    /*
     * Search through all 8 ports (0-7).
     *
     * The original walks the pointer table at 0xE26EE8 -- ROUTE_$PORTP, an
     * array of route_$port_t * -- and compares the first longword of each
     * entry, which is route_$port_t.network (offset 0x00, asserted in
     * route/route.h).  The table is addressed here through the typed global
     * instead of the absolute address, so the loop is architecture neutral.
     *
     * Note that the entries are dereferenced unconditionally: a null slot
     * would fault, exactly as in the original.
     */
    for (i = 0; i <= 7; i++) {
        port_net_id = (int32_t)ROUTE_$WIRED_DATA.portp[i]->network;

        if (network == port_net_id) {
            *port_ret = i;
            return;
        }
    }

    /* Not found, *port_ret remains -1 */
}
