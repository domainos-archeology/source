/*
 * NETWORK_$GET_PKT_SIZE - Get maximum packet size for destination
 *
 * Determines the appropriate packet size to use when communicating with
 * a network destination. The function returns:
 *   - max_size for local node (loopback) destinations
 *   - 0x400 (minimum) for all other destinations
 *
 * The logic checks if the destination is the local node (NODE_$ME), either
 * directly or via the loopback flag. If so, the caller's requested max_size
 * is returned (clamped to >= 0x400). For all other destinations, including
 * remote nodes on local ports or routed destinations, the minimum packet
 * size of 0x400 is used to ensure compatibility.
 *
 * Original address: 0x00E0FA00
 */

#include "network/network_internal.h"
#include "route/route.h"
#include "rip/rip.h"

/*
 * Minimum/default packet size
 */
#define PKT_SIZE_MIN    0x400

/*
 * NETWORK_$GET_PKT_SIZE - Get maximum packet size for destination
 *
 * @param dest_addr     Pointer to destination address structure:
 *                        +0x00: network port/type (4 bytes)
 *                        +0x04: node ID (4 bytes - low 20 bits used)
 * @param max_size      Caller's requested maximum packet size
 *
 * @return Packet size to use:
 *         - max_size (clamped to >= 0x400) for local node destinations
 *         - 0x400 for all remote destinations
 */
uint16_t NETWORK_$GET_PKT_SIZE(uint32_t *dest_addr, uint16_t max_size)
{
    uint16_t result;
    uint16_t rip_result;
    uint16_t port_num;
    status_$t status;
    uint8_t nexthop_info[16];   /* Next hop address buffer (10 bytes used) */
    uint32_t local_addr[3];     /* Local copy of address for route lookup */
    uint32_t node_id;
    uint8_t metric;
    route_$port_t *port_ptr;

    /* Initialize result to caller's requested max_size */
    result = max_size;

    /* If already at minimum size, no optimization needed */
    if (max_size == PKT_SIZE_MIN) {
        goto done_clamp;
    }

    /* Extract the network port type from destination */
    local_addr[0] = dest_addr[0];

    /*
     * Only process if using ROUTE_$PORT or unspecified (0).
     * Other port types indicate a different routing path.
     */
    if (local_addr[0] != ROUTE_$PORT && local_addr[0] != 0) {
        result = PKT_SIZE_MIN;
        goto done_clamp;
    }

    /* Determine the node ID to use for route lookup */
    if (NETWORK_$LOOPBACK_FLAG < 0) {
        /* Loopback mode: use local node regardless of destination */
        node_id = NODE_$ME;
    } else {
        /* Normal mode: use the destination node ID */
        node_id = dest_addr[1];
    }

    /*
     * Build the address structure for RIP lookup.
     * The structure is 10 bytes: 4-byte port + 6-byte address.
     * We use the masked node_id (low 20 bits) as part of the address.
     */

    /* Find the next hop for this destination */
    metric = RIP_$FIND_NEXTHOP(&local_addr[0], 0, &port_num, nexthop_info, &status);

    /* Save the result based on status */
    rip_result = metric;
    if (status != status_$ok) {
        /* No route found - will use minimum size if not local */
        rip_result = PKT_SIZE_MIN;
    }

    /*
     * If destination is the local node, return the requested max_size.
     * This applies regardless of routing status - local traffic can
     * use larger packets.
     */
    if (node_id == NODE_$ME) {
        /* result stays as max_size */
        goto done_clamp;
    }

    /*
     * For non-local destinations, check the route type.
     */
    if (metric == 0) {
        /*
         * Direct route (metric 0) - check port type.
         * Even for direct routes, non-local ports use minimum size.
         */
        result = rip_result;
        port_ptr = ROUTE_$PORTP[port_num];

        if (port_ptr->active != ROUTE_PORT_TYPE_LOCAL) {
            /* Non-local port: use minimum packet size */
            result = PKT_SIZE_MIN;
        }
        /* For local ports, result = rip_result (0 or 0x400), clamped below */
        goto done_clamp;
    }

    /* Indirect route (metric > 0): use minimum size */
    result = PKT_SIZE_MIN;

done_clamp:
    /* Clamp result to be at most max_size */
    if (result > max_size) {
        result = max_size;
    }

    /* Ensure minimum packet size of 0x400 */
    if (result <= PKT_SIZE_MIN) {
        result = PKT_SIZE_MIN;
    }

    return result;
}
