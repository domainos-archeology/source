/*
 * RIP_$FIND_NEXTHOP - Find next hop for destination
 *
 * This function determines how to route a packet to a destination address.
 * It first checks local ports (direct connectivity), then falls back to
 * the routing table for indirect routes.
 *
 * Original address: 0x00E15696
 */

#include "rip/rip_internal.h"

/*
 * Port table structure (for checking direct connectivity)
 *
 * Each port entry is 0x5c (92) bytes. The table has 8 entries.
 */
typedef struct route_$port_entry_t {
    uint32_t    network;        /* 0x00: Network address */
    uint8_t     _pad0[0x28];    /* 0x04: Unknown fields */
    uint16_t    flags;          /* 0x2C: Port flags (bits 2-5 indicate active) */
    uint8_t     _pad1[0x2E];    /* 0x2E: Remaining fields */
} route_$port_entry_t;

#define ROUTE_PORT_COUNT        8
#define ROUTE_PORT_ACTIVE_MASK  0x3C    /* Bits 2-5 indicate active port types */

#if defined(ARCH_M68K)
    #define ROUTE_$PORT_TABLE   ((route_$port_entry_t *)0xE2E0A0)
#else
    extern route_$port_entry_t ROUTE_$PORT_TABLE[];
#endif

/*
 * XNS address helper: copy 10 bytes (4 byte network + 6 byte host)
 */
static void copy_xns_addr(void *dst, const void *src)
{
    uint32_t *d32 = (uint32_t *)dst;
    uint32_t *s32 = (uint32_t *)src;

    d32[0] = s32[0];    /* Network (4 bytes) */
    d32[1] = s32[1];    /* Host bytes 0-3 */
    ((uint16_t *)dst)[4] = ((uint16_t *)src)[4];  /* Host bytes 4-5 */
}

/*
 * RIP_$FIND_NEXTHOP - Find next hop for destination
 *
 * Algorithm:
 * 1. Initialize output: port = 0, nexthop = destination, status = OK
 * 2. If destination network is 0 (broadcast/null):
 *    - Increment direct hit counter and return 0 (direct route)
 * 3. Check local port table for direct connectivity:
 *    - Scan 8 ports, looking for matching network with active flags
 *    - If found: set port number, increment direct hits, return 0
 * 4. Look up in routing table:
 *    - Acquire exclusion lock
 *    - Call RIP_$NET_LOOKUP with inc_refcount=0xFF, create=0
 *    - If entry found:
 *      - Select route based on flags (standard vs non-standard)
 *      - Check if route is valid (metric < 16, state 1 or 2)
 *      - If valid: set port from route, optionally copy next hop
 *      - Return metric
 *    - If not found: set status to error
 *    - Release exclusion lock
 * 5. Return metric (0 for direct, >0 for indirect, 0 with error for no route)
 */
uint8_t RIP_$FIND_NEXTHOP(void *addr_info, int8_t flags, uint16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    uint32_t *src_addr = (uint32_t *)addr_info;
    uint32_t dest_network;
    uint16_t port_idx;
    route_$port_entry_t *port_entry;
    rip_$entry_t *rip_entry;
    rip_$route_t *route;
    uint8_t metric;
    uint8_t state;

    /* Initialize outputs */
    *port_ret = 0;
    copy_xns_addr(nexthop_ret, addr_info);  /* Default: route directly to dest */
    *status_ret = status_$ok;

    /* Get destination network */
    dest_network = src_addr[0];

    /* Check for null/broadcast network */
    if (dest_network == 0) {
        RIP_$DATA.direct_hits++;
        return 0;  /* Direct route */
    }

    /*
     * Check local port table for direct connectivity.
     * Port flags bits 2-5 (mask 0x3C) indicate active port types.
     */
    port_entry = &ROUTE_$PORT_TABLE[0];
    for (port_idx = 0; port_idx < ROUTE_PORT_COUNT; port_idx++) {
        /* Check if port is active (any of bits 2-5 set) */
        if ((port_entry->flags & ROUTE_PORT_ACTIVE_MASK) != 0) {
            /* Check for network match */
            if (port_entry->network == dest_network) {
                /* Direct connectivity via this port */
                RIP_$DATA.direct_hits++;
                *port_ret = port_idx;
                return 0;  /* Direct route */
            }
        }
        port_entry++;
    }

    /*
     * No direct route - look up in routing table.
     * Need to hold exclusion lock while accessing routing table.
     */
    ML_$EXCLUSION_START(&RIP_$DATA.exclusion);

    /* Look up network (don't create, don't increment ref count) */
    rip_entry = RIP_$NET_LOOKUP(dest_network, 0xFF, 0);

    if (rip_entry != NULL) {
        /* Select route based on flags parameter */
        if (flags < 0) {
            /* Non-standard route (slot 1) */
            route = &rip_entry->routes[1];
        } else {
            /* Standard route (slot 0) */
            route = &rip_entry->routes[0];
        }

        /* Get metric and state */
        metric = route->metric;
        state = (route->flags >> RIP_STATE_SHIFT) & 0x03;

        /*
         * Check if route is usable:
         * - Metric must be less than infinity (0x10 = 16)
         * - State must be VALID (1) or AGING (2)
         */
        if (metric < 0x10 && (state == RIP_STATE_VALID || state == RIP_STATE_AGING)) {
            /* Set output port */
            *port_ret = route->port;

            /*
             * If metric is non-zero, this is an indirect route.
             * Copy the next hop address from the routing table.
             * (If metric is 0, keep the original destination as next hop)
             */
            if (metric != 0) {
                copy_xns_addr(nexthop_ret, &route->nexthop);
            }

            /* Success - return metric */
            goto done;
        }
    }

    /* No route found */
    *status_ret = RIP_$STATUS_NO_ROUTE;
    metric = 0;

done:
    ML_$EXCLUSION_STOP(&RIP_$DATA.exclusion);
    return metric;
}
