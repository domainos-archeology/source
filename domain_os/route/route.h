/*
 * ROUTE - Network Routing
 *
 * This module provides network routing information.
 */

#ifndef ROUTE_H
#define ROUTE_H

#include "base/base.h"

/*
 * Port structure (0x5C = 92 bytes)
 *
 * Each network port has associated configuration including network
 * and socket identifiers. The system supports up to 8 ports.
 *
 * Note: This structure is partially understood. Fields at offsets
 * 0x2C, 0x2E, and 0x30 are known; others need more analysis.
 */
typedef struct route_$port_t {
    uint8_t     _unknown0[0x2C];    /* 0x00: Unknown fields */
    uint16_t    active;             /* 0x2C: Non-zero if port is active */
    uint16_t    network;            /* 0x2E: Network identifier */
    uint16_t    socket;             /* 0x30: Socket identifier */
    uint8_t     _unknown1[0x28];    /* 0x32: Unknown fields (to 0x5C) */
} route_$port_t;

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
 * ROUTE_$PORTP - Array of pointers to port structures
 *
 * Array of 8 pointers to route_$port_t structures, one for each
 * possible network port. Used by ROUTE_$FIND_PORT to look up
 * port info by index.
 *
 * Original address: 0xE26EE8
 */
#if defined(M68K)
#define ROUTE_$PORTP            ((route_$port_t **)0xE26EE8)
#else
extern route_$port_t **ROUTE_$PORTP;
#endif

/* Number of network ports supported */
#define ROUTE_$MAX_PORTS        8

/*
 * ROUTE_$FIND_PORT - Find port index by network/socket
 *
 * Searches the port array for a port matching the given network
 * and socket identifiers.
 *
 * @param network   Network identifier to match
 * @param socket    Socket identifier to match
 *
 * @return Port index (0-7) if found, -1 (0xFF) if not found
 *
 * Original address: 0x00E15AF8
 */
int8_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket);

#endif /* ROUTE_H */
