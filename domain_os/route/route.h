/*
 * ROUTE - Network Routing Port Management Module
 *
 * This module provides port management and routing services for network
 * communication in Domain/OS. It manages routing ports, handles port
 * lookups, and provides event count registration for asynchronous I/O.
 *
 * The ROUTE subsystem maintains up to 8 routing ports, each with its
 * associated network configuration and socket bindings.
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
 * Layout (partially decoded):
 *   +0x00: network address (4 bytes)
 *   +0x2C: active status (2 bytes) - non-zero if port active
 *   +0x2E: port type/network (2 bytes) - 1=local, 2=routing
 *   +0x30: socket identifier (2 bytes)
 *   +0x36: secondary socket (2 bytes)
 *   +0x38: port event count structure (0x24 bytes)
 */
typedef struct route_$port_t {
    uint32_t    network;            /* 0x00: Network address */
    uint8_t     _unknown0[0x28];    /* 0x04: Unknown fields */
    uint16_t    active;             /* 0x2C: Non-zero if port is active */
    uint16_t    port_type;          /* 0x2E: Port type (1=local, 2=routing) */
    uint16_t    socket;             /* 0x30: Socket identifier */
    uint8_t     _unknown1[0x04];    /* 0x32: Unknown fields */
    uint16_t    socket2;            /* 0x36: Secondary socket */
    uint8_t     port_ec[0x24];      /* 0x38: Port event count structure */
} route_$port_t;

/* Port type constants */
#define ROUTE_PORT_TYPE_LOCAL       1
#define ROUTE_PORT_TYPE_ROUTING     2

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
 * Short port info structure (12 bytes)
 *
 * Compact representation of port information used for passing
 * port data between functions.
 */
typedef struct route_$short_port_t {
    uint32_t    network;            /* 0x00: Network address */
    uint32_t    host_id;            /* 0x04: Host ID (from port+0x2c) */
    uint16_t    network2;           /* 0x08: Secondary network (from port+0x30) */
    uint16_t    socket;             /* 0x0A: Socket (from port+0x36) */
} route_$short_port_t;

/*
 * ROUTE_$FIND_PORT - Find port index by network/socket
 *
 * Searches the port array for a port matching the given network
 * and socket identifiers.
 *
 * @param network   Network identifier to match
 * @param socket    Socket identifier to match (sign-extended to 32-bit)
 *
 * @return Port index (0-7) if found, -1 if not found
 *
 * Original address: 0x00E15AF8
 */
int16_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket);

/*
 * ROUTE_$FIND_PORTP - Find port structure by network/socket
 *
 * Similar to ROUTE_$FIND_PORT, but returns a pointer to the port
 * structure instead of the port index. Useful when direct access
 * to the port structure is needed.
 *
 * @param network   Network identifier to match
 * @param socket    Socket identifier to match (sign-extended to 32-bit)
 *
 * @return Pointer to port structure if found, NULL if not found
 *
 * Original address: 0x00E15B46
 */
route_$port_t *ROUTE_$FIND_PORTP(uint16_t network, int32_t socket);

/*
 * ROUTE_$SHORT_PORT - Extract short port info from port structure
 *
 * Copies key fields from a full port structure into a compact 12-byte
 * format suitable for passing to other functions.
 *
 * Output format (12 bytes):
 *   +0x00: network (4 bytes) - from port_struct+0x00
 *   +0x04: host ID (4 bytes) - from port_struct+0x2C
 *   +0x08: network2 (2 bytes) - from port_struct+0x30
 *   +0x0A: socket (2 bytes) - from port_struct+0x36
 *
 * @param port_struct   Source port structure pointer
 * @param short_info    Output: 12-byte compact port info
 *
 * Original address: 0x00E69C08
 */
void ROUTE_$SHORT_PORT(route_$port_t *port_struct, route_$short_port_t *short_info);

/*
 * ROUTE_$GET_EC - Get event count for a port
 *
 * Registers and returns an event count for the specified port.
 * The port is identified by network/socket pair within the port_info
 * structure. Supports two EC types: socket EC (type 0) and port EC (type 1).
 *
 * @param port_info   Port information structure with network at +6, socket at +8
 * @param ec_type     Pointer to EC type: 0 = socket EC, 1 = port EC
 * @param ec_ret      Output: pointer to registered event count
 * @param status_ret  Output: status code
 *
 * Status codes:
 *   status_$ok: Success
 *   status_$internet_unknown_network_port (0x2B0003): Port not found
 *   status_$route_not_routing_mode (0x2B0009): Port not in routing mode
 *   status_$route_invalid_ec_type (0x2B0012): Invalid EC type
 *
 * Original address: 0x00E69C2C
 */
void ROUTE_$GET_EC(void *port_info, int16_t *ec_type, void **ec_ret,
                   status_$t *status_ret);

/*
 * ROUTE_$SERVICE - Main routing service entry point
 *
 * Handles routing service requests for a specific port. This is the
 * central function for managing route operations.
 *
 * @param operation     Service operation code/type
 * @param port_info     12-byte compact port info (from ROUTE_$SHORT_PORT)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E6A030
 */
void ROUTE_$SERVICE(void *operation, void *port_info, status_$t *status_ret);

/*
 * ROUTE_$SHUTDOWN - Shutdown all routing ports
 *
 * Iterates through all active routing ports and calls ROUTE_$SERVICE
 * to shut them down gracefully. Uses different shutdown codes based
 * on port type:
 *   - Port type 1 (local) or 2 (routing): uses operation code at 0xe6a65a
 *   - Other types: uses operation code at 0xe6a65c with
 *                  shutdown type 2 (first port) or 1 (subsequent)
 *
 * Original address: 0x00E6A5DC
 */
void ROUTE_$SHUTDOWN(void);

/*
 * ROUTE_$READ_USER_STATS - Read user-visible routing statistics
 *
 * Retrieves routing statistics for user-space access.
 *
 * Original address: 0x00E6A65E
 */
void ROUTE_$READ_USER_STATS(void);

/*
 * ROUTE_$PROCESS - Process routing updates
 *
 * Main processing function for handling routing protocol updates.
 *
 * Original address: 0x00E873EC
 */
void ROUTE_$PROCESS(void);

/*
 * ROUTE_$INCOMING - Handle incoming routed packets
 *
 * Processes packets that arrive via routing.
 *
 * Original address: 0x00E878A8
 */
void ROUTE_$INCOMING(void);

/*
 * ROUTE_$OUTGOING - Handle outgoing routed packets
 *
 * Prepares packets for transmission via routing.
 *
 * Original address: 0x00E87A4E
 */
void ROUTE_$OUTGOING(void);

/*
 * ROUTE_$SEND_USER_PORT - Send packet to user port
 *
 * Transmits a packet through a user-specified routing port.
 *
 * Original address: 0x00E87C34
 */
void ROUTE_$SEND_USER_PORT(void);

/*
 * Status codes
 */
#define status_$internet_unknown_network_port   0x2B0003
#define status_$route_not_routing_mode          0x2B0009
#define status_$route_invalid_ec_type           0x2B0012

#endif /* ROUTE_H */
