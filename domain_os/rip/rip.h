/*
 * RIP - Routing Information Protocol Module
 *
 * This module provides routing table management and network route lookup.
 */

#ifndef RIP_H
#define RIP_H

#include "base/base.h"

/*
 * RIP_$FIND_NEXTHOP - Find next hop for destination
 *
 * Looks up the routing table to find the next hop for a given destination.
 *
 * @param addr_info     Address information structure
 * @param flags         Lookup flags
 * @param port_ret      Output: port number
 * @param nexthop_ret   Output: next hop address (6 bytes)
 * @param status_ret    Output: status code
 *
 * Returns:
 *   0 on direct route (same network)
 *   Non-zero on indirect route (via gateway)
 *
 * Original address: 0x00E15696
 */
int16_t RIP_$FIND_NEXTHOP(void *addr_info, uint16_t flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret);

/*
 * RIP_$NET_LOOKUP - Look up network in routing table
 *
 * @param network       Network address to look up
 * @param entry_ret     Output: routing table entry
 *
 * Original address: 0x00E154E4
 */
void RIP_$NET_LOOKUP(uint32_t network, void *entry_ret);

/*
 * RIP_$INIT - Initialize RIP subsystem
 *
 * Original address: 0x00E2FBD0
 */
void RIP_$INIT(void);

#endif /* RIP_H */
