/*
 * ROUTE_$VALIDATE_PORT - Check network capability for node
 *
 * Checks if a network operation is supported for the given routing info.
 * Iterates through ROUTE_$PORTP array looking for matching port.
 *
 * Parameters:
 *   routing_key - Routing information (port network address)
 *   is_local    - Non-zero if querying local node
 *
 * Returns:
 *   0: Unknown network
 *   1: Network supports operation
 *   2: Operation not defined on hardware
 *
 * Original address: 0x00E65904
 * Original size: 118 bytes
 */

#include "route/route_internal.h"

int16_t ROUTE_$VALIDATE_PORT(int32_t routing_key, int8_t is_local)
{
    route_$port_t *port = NULL;
    int16_t i;

    if (routing_key == 0) {
        /* Check first port for active status */
        if (ROUTE_$PORTP[0]->active == 0) {
            return 0;  /* Unknown network */
        }
        port = ROUTE_$PORTP[0];
    } else {
        /* Search through all 8 ports for matching network address */
        for (i = 7; i >= 0; i--) {
            route_$port_t *p = ROUTE_$PORTP[i];
            if (p->active != 0 && (int32_t)p->network == routing_key) {
                port = p;
                break;
            }
        }
    }

    if (port == NULL) {
        /* No matching port found */
        if (is_local < 0) {
            return 2;  /* Operation not defined on hardware */
        }
        /* Fall through to return 1 */
    } else {
        /*
         * Check driver capability flags at port->driver_info + 7
         * Bit 1 indicates hardware support for operation
         */
        uint8_t *driver_flags = (uint8_t *)(*((uint32_t *)((char *)port + 0x48)) + 7);
        if ((*driver_flags & 0x02) == 0) {
            return 2;  /* Operation not defined on hardware */
        }
    }

    return 1;  /* Network supports operation */
}
