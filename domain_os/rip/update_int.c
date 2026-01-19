/*
 * RIP_$UPDATE_INT - Internal route update function
 *
 * Updates routing table entries with new route information. This function
 * is called during initialization and when receiving routing updates from
 * other nodes.
 *
 * The function supports two modes:
 * - Single network update: Updates a specific network entry
 * - Bulk update: Updates all entries (when network == -1)
 *
 * Original address: 0x00E15922
 *
 * Helper functions (originally nested Pascal procedures):
 * - RIP_$COMPARE_SOURCE (0x00E15830): Compares route source addresses
 * - RIP_$APPLY_UPDATE (0x00E15888): Applies update to a route entry
 */

#include "rip/rip_internal.h"

/*
 * Standard RIP update logic:
 *
 * 1. Accept updates from the same source (they might be withdrawing the route)
 * 2. Accept better routes (lower metric)
 * 3. Accept non-infinity updates to non-valid routes
 *
 * When a route source sends hop_count >= 16 (infinity), the route is being
 * withdrawn and transitions to AGING state with a short timeout.
 */

/*
 * Route aging timeout (short) - used when a route is being invalidated.
 * This is 40 ticks vs the normal 360 ticks for valid routes.
 */
#define RIP_AGING_TIMEOUT       0x28

/*
 * RIP_$COMPARE_SOURCE - Compare route source addresses
 *
 * Compares the source address of an existing route with a new source
 * to determine if the update is from the same source.
 *
 * For non-standard routes (flags < 0):
 *   Compares full 6-byte XNS host address
 *
 * For standard routes (flags >= 0):
 *   Compares only lower 20 bits of host ID
 *
 * @param route_nexthop  Pointer to existing route's nexthop address
 * @param source         Pointer to new source address (10 bytes)
 * @param flags          Route type flags (negative = non-standard)
 *
 * @return 0xFF if same source, 0 otherwise
 *
 * Original address: 0x00E15830
 */
static int8_t rip_$compare_source(rip_$xns_addr_t *route_nexthop,
                                   rip_$xns_addr_t *source,
                                   int8_t flags)
{
    if (flags < 0) {
        /*
         * Non-standard route: compare full 6-byte host address.
         * This is done as 3 word comparisons in the original assembly.
         */
        if (route_nexthop->host[0] != source->host[0] ||
            route_nexthop->host[1] != source->host[1]) {
            return 0;
        }
        if (route_nexthop->host[2] != source->host[2] ||
            route_nexthop->host[3] != source->host[3]) {
            return 0;
        }
        if (route_nexthop->host[4] != source->host[4] ||
            route_nexthop->host[5] != source->host[5]) {
            return 0;
        }
        return (int8_t)0xFF;  /* Match */
    } else {
        /*
         * Standard route: compare only lower 20 bits of host ID.
         * The host address is 6 bytes; we compare the 20-bit portion
         * at offset +2 within the host field (bytes 2-5, masked).
         *
         * Original assembly: AND with 0xFFFFF and compare
         * This extracts bits from the host field starting at offset +2.
         */
        uint32_t route_id = ((uint32_t)route_nexthop->host[2] << 24) |
                            ((uint32_t)route_nexthop->host[3] << 16) |
                            ((uint32_t)route_nexthop->host[4] << 8) |
                            ((uint32_t)route_nexthop->host[5]);
        uint32_t source_id = ((uint32_t)source->host[2] << 24) |
                             ((uint32_t)source->host[3] << 16) |
                             ((uint32_t)source->host[4] << 8) |
                             ((uint32_t)source->host[5]);

        if ((route_id & 0xFFFFF) == (source_id & 0xFFFFF)) {
            return (int8_t)0xFF;  /* Match */
        }
        return 0;
    }
}

/*
 * RIP_$APPLY_UPDATE - Apply route update to an entry
 *
 * Applies a routing update to a route entry. Handles three cases:
 *
 * 1. Route withdrawal: If the existing route was VALID with metric 0
 *    and new metric >= 16 (infinity), transition to AGING with short timeout.
 *
 * 2. Normal update: Copy source info, port, metric from the update.
 *    Set state to VALID with normal timeout.
 *
 * Also sets the recent_changes flag if the metric changed, to trigger
 * sending of routing updates to neighbors.
 *
 * @param route       Pointer to the route entry to update
 * @param source      Pointer to source address (10 bytes)
 * @param hop_count   New hop count / metric
 * @param port_index  Port index for this route
 * @param flags       Route type flags (negative = non-standard)
 *
 * Original address: 0x00E15888
 */
static void rip_$apply_update(rip_$route_t *route,
                               rip_$xns_addr_t *source,
                               uint8_t hop_count,
                               uint8_t port_index,
                               int8_t flags)
{
    uint8_t state;
    uint8_t old_metric = route->metric;

    /* Check if metric changed - set recent_changes flag */
    if (old_metric != hop_count) {
        if (flags < 0) {
            /* Non-standard route */
            RIP_$DATA.std_recent_changes = 0xFF;
        } else {
            /* Standard route */
            RIP_$DATA.recent_changes = 0xFF;
        }
    }

    /*
     * Check for route invalidation case:
     * - Current metric is 0 (direct route)
     * - Current state is VALID
     * - New hop count >= 16 (infinity)
     *
     * In this case, transition to AGING state with short timeout
     * rather than immediately invalidating.
     */
    if (old_metric == 0) {
        state = (route->flags >> RIP_STATE_SHIFT) & 0x03;
        if (state == RIP_STATE_VALID && hop_count >= 16) {
            /* Set state to AGING */
            route->flags = (route->flags & ~RIP_STATE_MASK) |
                           (RIP_STATE_AGING << RIP_STATE_SHIFT);
            /* Set short timeout */
            route->expiration = TIME_$CLOCKH + RIP_AGING_TIMEOUT;
            return;
        }
    }

    /*
     * Normal update: copy route information
     */

    /* Copy 10-byte source address to nexthop */
    route->nexthop.network = source->network;
    route->nexthop.host[0] = source->host[0];
    route->nexthop.host[1] = source->host[1];
    route->nexthop.host[2] = source->host[2];
    route->nexthop.host[3] = source->host[3];
    route->nexthop.host[4] = source->host[4];
    route->nexthop.host[5] = source->host[5];

    /* Copy port and metric */
    route->port = port_index;
    route->metric = hop_count;

    /* Set state to VALID */
    route->flags = (route->flags & ~RIP_STATE_MASK) |
                   (RIP_STATE_VALID << RIP_STATE_SHIFT);

    /* Set normal timeout */
    route->expiration = TIME_$CLOCKH + RIP_ROUTE_TIMEOUT;
}

/*
 * RIP_$UPDATE_INT - Internal route update function
 *
 * @param network      Network to update (-1 for all entries, 0 = no-op)
 * @param source       Pointer to source address (10 bytes)
 * @param hop_count    New hop count / metric (clamped to 17)
 * @param port_index   Port index for this route
 * @param flags        If negative, use non-standard routes; else standard
 * @param status_ret   Output: status code
 *
 * When network == -1:
 *   Updates all entries where the source matches and state is not UNUSED.
 *   This is used to refresh routes from a particular source.
 *
 * When network is a specific value:
 *   Looks up or creates the entry for that network and updates it if:
 *   - Source matches (same source is updating its route), OR
 *   - New metric is better (lower), OR
 *   - Entry is not VALID and new metric is not infinity
 */
void RIP_$UPDATE_INT(uint32_t network, rip_$xns_addr_t *source,
                     uint16_t hop_count, uint8_t port_index,
                     int8_t flags, status_$t *status_ret)
{
    int i;
    rip_$entry_t *entry;
    rip_$route_t *route;
    uint8_t state;
    int8_t same_source;
    uint8_t clamped_hop_count;

    /* Initialize status */
    *status_ret = status_$ok;

    /* No-op if network is 0 */
    if (network == 0) {
        return;
    }

    /* Clamp hop count to RIP infinity (17) */
    clamped_hop_count = (hop_count > RIP_INFINITY) ? RIP_INFINITY : (uint8_t)hop_count;

    /* Acquire RIP lock */
    RIP_$LOCK();

    if (network == (uint32_t)-1) {
        /*
         * Update all entries mode:
         * Walk through all 64 entries and update routes where:
         * - Source matches the provided source
         * - State is not UNUSED
         */
        entry = &RIP_$DATA.entries[0];

        for (i = RIP_TABLE_SIZE - 1; i >= 0; i--) {
            /* Select standard or non-standard route based on flags */
            if (flags < 0) {
                route = &entry->routes[1];  /* Non-standard at offset 0x18 */
            } else {
                route = &entry->routes[0];  /* Standard at offset 0x04 */
            }

            /* Check if source matches */
            same_source = rip_$compare_source(&route->nexthop, source, flags);

            if (same_source < 0) {
                /* Source matches - check if state is non-UNUSED */
                state = (route->flags >> RIP_STATE_SHIFT) & 0x03;
                if (state != RIP_STATE_UNUSED) {
                    /* Apply the update */
                    rip_$apply_update(route, source, clamped_hop_count,
                                       port_index, flags);
                }
            }

            entry++;
        }
    } else {
        /*
         * Single network update mode:
         * Look up the specific network and update if conditions are met.
         */
        entry = RIP_$NET_LOOKUP(network, 0, (int16_t)0xFF54);

        if (entry == NULL) {
            /* Could not find or create entry - table is full */
            *status_ret = status_$network_too_many_networks_in_internet;
        } else {
            /* Select standard or non-standard route */
            if (flags < 0) {
                route = &entry->routes[1];  /* Non-standard */
            } else {
                route = &entry->routes[0];  /* Standard */
            }

            /* Check if we should apply this update */
            same_source = rip_$compare_source(&route->nexthop, source, flags);

            /*
             * Apply update if any of these conditions are true:
             * 1. Same source (they might be withdrawing the route)
             * 2. New metric is better (lower) than current
             * 3. Current route is not VALID and new metric is not infinity
             */
            if (same_source < 0) {
                /* Same source - always accept */
                rip_$apply_update(route, source, clamped_hop_count,
                                   port_index, flags);
            } else if (route->metric > clamped_hop_count) {
                /* Better metric - accept */
                rip_$apply_update(route, source, clamped_hop_count,
                                   port_index, flags);
            } else {
                /* Check state and metric conditions */
                state = (route->flags >> RIP_STATE_SHIFT) & 0x03;

                if (state != RIP_STATE_VALID && clamped_hop_count <= 16) {
                    /* Non-valid entry and non-infinity metric - accept */
                    rip_$apply_update(route, source, clamped_hop_count,
                                       port_index, flags);
                }
            }
        }
    }

    /* Release RIP lock */
    RIP_$UNLOCK();
}
