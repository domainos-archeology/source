/*
 * RIP - Routing Information Protocol Module
 *
 * This module provides routing table management and network route lookup
 * for XNS/IDP networking in Domain/OS.
 *
 * RIP is a distance-vector routing protocol that maintains a table of
 * reachable networks and their metrics (hop counts). The implementation
 * supports:
 * - Hash table lookup for destination networks
 * - Route aging and expiration
 * - Separate routes for standard and non-standard traffic types
 */

#ifndef RIP_H
#define RIP_H

#include "base/base.h"

/* Forward declaration for opaque entry type */
struct rip_$entry_t;

/*
 * RIP_$NET_LOOKUP - Look up network in routing table
 *
 * Searches the routing table for an entry matching the given network.
 * Uses a hash table with linear probing (64 entries, hash = network & 0x3F).
 *
 * Behavior depends on flags:
 * - If entry found and inc_refcount < 0: increments reference count
 * - If not found and create_if_missing < 0: creates new entry
 * - If creating and inc_refcount < 0: clears reference count
 * - If creating and inc_refcount >= 0: sets reference count to 1
 *
 * @param network           Network address to look up
 * @param inc_refcount      If < 0, increment reference count on find/create
 * @param create_if_missing If < 0, create entry if not found
 *
 * @return Pointer to routing table entry, or NULL if not found/created
 *
 * Original address: 0x00E154E4
 */
struct rip_$entry_t *RIP_$NET_LOOKUP(uint32_t network, int8_t inc_refcount,
                                      int16_t create_if_missing);

/*
 * RIP_$FIND_NEXTHOP - Find next hop for destination
 *
 * Looks up the routing table to find the next hop for a given destination.
 * First checks local ports (direct connections), then queries the routing table.
 *
 * @param addr_info     Source address information (10 bytes: 4 byte net + 6 byte host)
 * @param flags         If < 0, use non-standard routes; else use standard routes
 * @param port_ret      Output: port number for routing
 * @param nexthop_ret   Output: next hop address (10 bytes, may be modified if indirect)
 * @param status_ret    Output: status code (status_$ok or 0x3C0001 for no route)
 *
 * @return 0 on direct route (same network), non-zero metric on indirect route
 *
 * Original address: 0x00E15696
 */
uint8_t RIP_$FIND_NEXTHOP(void *addr_info, int8_t flags, uint16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret);

/*
 * RIP_$INIT - Initialize RIP subsystem
 *
 * Initializes the RIP routing subsystem including:
 * - Clearing the routing table
 * - Initializing exclusion locks
 * - Setting up data structures
 *
 * Original address: 0x00E2FBD0
 */
void RIP_$INIT(void);

/*
 * RIP_$AGE - Age routing table entries
 *
 * Called periodically to age routing table entries. Routes that have
 * not been refreshed will eventually expire and be removed.
 *
 * Original address: 0x00E155C0
 */
void RIP_$AGE(void);

/*
 * RIP_$UPDATE - Update routing table entry (simplified interface)
 *
 * Updates a routing table entry using port index directly. Constructs
 * the source address from the port's network number and the provided
 * host ID. Always updates standard routes (flags=0).
 *
 * @param network_ptr     Pointer to destination network (4 bytes)
 * @param host_id_ptr     Pointer to source host ID (low 20 bits used)
 * @param hop_count_ptr   Pointer to hop count / metric (2 bytes)
 * @param port_index_ptr  Pointer to port index (0-7)
 * @param status_ret      Output: status code
 *
 * Original address: 0x00E690EE
 */
void RIP_$UPDATE(uint32_t *network_ptr, uint32_t *host_id_ptr,
                 uint16_t *hop_count_ptr, int16_t *port_index_ptr,
                 status_$t *status_ret);

/*
 * RIP_$UPDATE_D - Update routing table entry (detailed interface)
 *
 * Updates a routing table entry using network/socket pair to identify
 * the port. The "D" suffix likely stands for "debug" or "detailed" as
 * this variant provides more explicit port identification.
 *
 * @param network_ptr    Pointer to destination network (4 bytes)
 * @param source         Pointer to source XNS address (10 bytes)
 * @param hop_count_ptr  Pointer to hop count / metric (2 bytes)
 * @param port_info      Pointer to structure containing:
 *                         +0x06: port network (2 bytes)
 *                         +0x08: port socket (2 bytes)
 * @param flags_ptr      Pointer to route type flags (1 byte):
 *                         If < 0: non-standard route
 *                         If >= 0: standard route
 * @param status_ret     Output: status code
 *
 * Status codes:
 *   status_$ok: Success
 *   status_$internet_unknown_network_port (0x2B0003): Port not found
 *
 * Original address: 0x00E69084
 */
void RIP_$UPDATE_D(uint32_t *network_ptr, void *source,
                   uint16_t *hop_count_ptr, uint8_t *port_info,
                   int8_t *flags_ptr, status_$t *status_ret);

/*
 * Status codes
 */
#define RIP_$STATUS_NO_ROUTE    0x3C0001    /* No route to destination */

#endif /* RIP_H */
