/*
 * ROUTE - Network Routing
 *
 * This module provides network routing information.
 */

#ifndef ROUTE_H
#define ROUTE_H

#include "base/base.h"

/*
 * ROUTE_$PORT - Current node's network port
 *
 * Contains the network port identifier for this node.
 * Set by HINT_$INIT from the hint file, or 0 if not available.
 *
 * Original address: 0xE2E0A0
 */
#if defined(M68K)
#define ROUTE_$PORT             (*(uint32_t *)0xE2E0A0)
#else
extern uint32_t ROUTE_$PORT;
#endif

/*
 * ROUTE_$PORTP - Pointer to network routing port information structure
 *
 * Points to a structure containing network port information.
 * Offset 0x2E contains network info (2 shorts).
 * Offset 0x30 contains additional network info.
 *
 * Original address: 0xE26EE8
 */
#if defined(M68K)
#define ROUTE_$PORTP            (*(uint8_t **)0xE26EE8)
#else
extern uint8_t *ROUTE_$PORTP;
#endif

#endif /* ROUTE_H */
