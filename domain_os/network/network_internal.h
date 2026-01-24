/*
 * NETWORK - Internal Header
 *
 * Internal definitions and helper prototypes for the NETWORK subsystem.
 * This file should be included by all .c files within the network/ directory.
 */

#ifndef NETWORK_INTERNAL_H
#define NETWORK_INTERNAL_H

#include "network/network.h"

/*
 * NODE_$ME - This node's identifier
 *
 * The low 20 bits of the local node's network address. Used to detect
 * loopback/local destination requests.
 *
 * Original address: 0xE245A4
 */
#if defined(M68K)
#define NODE_$ME        (*(uint32_t *)0xE245A4)
#else
extern uint32_t NODE_$ME;
#endif

/*
 * NETWORK_$LOOPBACK_FLAG - Loopback mode indicator
 *
 * If negative (bit 7 set), network operations should use the local node
 * as the destination instead of the specified address.
 *
 * Original address: 0xE24C44
 */
#if defined(M68K)
#define NETWORK_$LOOPBACK_FLAG  (*(int8_t *)0xE24C44)
#else
extern int8_t NETWORK_$LOOPBACK_FLAG;
#endif

#endif /* NETWORK_INTERNAL_H */
