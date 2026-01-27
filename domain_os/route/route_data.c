#include "route_internal.h"

/*
 * ROUTE_$PORT_ARRAY - Array of routing port structures
 *
 * Array of 8 port structures, each 0x5C (92) bytes.
 * Total size: 8 * 92 = 736 bytes (0x2E0)
 *
 * Original address: 0xE2E0A0
 */
route_$port_t ROUTE_$PORT_ARRAY[]; // TODO we need a size here...

/*
 * ROUTE_$SOCK_ECVAL - Socket event count value
 *
 * First 4 bytes contain a socket EC value.
 * Following 8 uint32_t pointers (at offset 0x04) point to port structures.
 *
 * Layout:
 *   +0x00: Socket EC value (4 bytes)
 *   +0x04: Pointer to port[0] structure
 *   +0x08: Pointer to port[1] structure
 *   ...
 *   +0x20: Pointer to port[7] structure
 *
 * Original address: 0xE26EE4
 */
uint32_t ROUTE_$SOCK_ECVAL;

/*
 * ROUTE_$SERVICE_MUTEX - Mutex for route service operations
 *
 * Original address: 0xE26280
 */
uint32_t ROUTE_$SERVICE_MUTEX;

/*
 * ROUTE_$CONTROL_ECVAL - Control event count value
 *
 * Original address: 0xE26F08
 */
uint32_t ROUTE_$CONTROL_ECVAL;

/*
 * ROUTE_$CONTROL_EC - Control event count
 *
 * Original address: 0xE26F0C
 */
uint32_t ROUTE_$CONTROL_EC;

/*
 * ROUTE_$SOCK - Socket reference
 *
 * Original address: 0xE26F18
 */
uint16_t ROUTE_$SOCK;

/*
 * ROUTE_$STD_N_ROUTING_PORTS - Standard number of routing ports
 *
 * Original address: 0xE26F1A
 */
int16_t ROUTE_$STD_N_ROUTING_PORTS;

/*
 * ROUTE_$N_ROUTING_PORTS - Current number of routing ports
 *
 * Original address: 0xE26F1C
 */
int16_t ROUTE_$N_ROUTING_PORTS;

/*
 * ROUTE_$ROUTING - Routing table/flag
 *
 * Original address: 0xE26F1E
 */
uint16_t ROUTE_$ROUTING;

/*
 * ROUTE_$PORTP - Array of pointers to port structures
 *
 * Array of 8 pointers to route_$port_t structures, one for each
 * possible network port. Used by ROUTE_$FIND_PORT to look up
 * port info by index.
 *
 * Original address: 0xE26EE8
 */
route_$port_t *ROUTE_$PORTP[ROUTE_$MAX_PORTS];
