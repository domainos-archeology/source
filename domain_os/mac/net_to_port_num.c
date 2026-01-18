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
     * ROUTE_$PORTP is an array of pointers to port info structures.
     * The first long (offset 0) of each port info is the network ID.
     */
#if defined(M68K)
    for (i = 0; i <= 7; i++) {
        /* Get port info pointer from ROUTE_$PORTP array */
        void *port_info = *(void **)(0xE26EE8 + i * 4);

        /* Get network ID from port info (offset 0) */
        port_net_id = *(int32_t *)port_info;

        if (network == port_net_id) {
            *port_ret = i;
            return;
        }
    }
#else
    /* Non-M68K stub */
    for (i = 0; i <= 7; i++) {
        /* TODO: Implement port table access for non-M68K */
    }
#endif

    /* Not found, *port_ret remains -1 */
}
