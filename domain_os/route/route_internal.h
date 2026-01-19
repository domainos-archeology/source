/*
 * ROUTE - Internal Definitions
 *
 * Internal data structures, globals, and helper functions used only
 * within the ROUTE subsystem. External users should use route.h instead.
 */

#ifndef ROUTE_INTERNAL_H
#define ROUTE_INTERNAL_H

#include "route/route.h"
#include "ec/ec.h"

/*
 * =============================================================================
 * Constants
 * =============================================================================
 */

/* Maximum number of routing ports */
#define ROUTE_MAX_PORTS         8

/* Port size in bytes */
#define ROUTE_PORT_SIZE         0x5C

/*
 * =============================================================================
 * Global Data (m68k addresses)
 * =============================================================================
 */

#if defined(M68K)
    /*
     * ROUTE_$PORT_ARRAY - Array of routing port structures
     *
     * Array of 8 port structures, each 0x5C (92) bytes.
     * Total size: 8 * 92 = 736 bytes (0x2E0)
     *
     * Original address: 0xE2E0A0
     */
    #define ROUTE_$PORT_ARRAY       ((route_$port_t *)0xE2E0A0)

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
    #define ROUTE_$SOCK_ECVAL       (*(uint32_t *)0xE26EE4)
    #define ROUTE_$PORTP_ARRAY      ((route_$port_t **)0xE26EE8)

    /*
     * SOCK_$EVENT_COUNTERS - Socket event counter array
     *
     * Array of pointers to event counters, indexed by socket number.
     *
     * Original address: 0xE28DB4
     */
    #define SOCK_$EVENT_COUNTERS    ((ec_$eventcount_t **)0xE28DB4)

    /*
     * ROUTE_$SERVICE_MUTEX - Mutex for route service operations
     *
     * Original address: 0xE26280
     */
    #define ROUTE_$SERVICE_MUTEX    (*(uint32_t *)0xE26280)

    /*
     * ROUTE_$CONTROL_ECVAL - Control error code value
     *
     * Original address: 0xE26F08
     */
    #define ROUTE_$CONTROL_ECVAL    (*(uint32_t *)0xE26F08)

    /*
     * ROUTE_$CONTROL_EC - Control error code
     *
     * Original address: 0xE26F0C
     */
    #define ROUTE_$CONTROL_EC       (*(uint32_t *)0xE26F0C)

    /*
     * ROUTE_$SOCK - Socket reference
     *
     * Original address: 0xE26F18
     */
    #define ROUTE_$SOCK             (*(uint16_t *)0xE26F18)

    /*
     * ROUTE_$STD_N_ROUTING_PORTS - Standard number of routing ports
     *
     * Original address: 0xE26F1A
     */
    #define ROUTE_$STD_N_ROUTING_PORTS  (*(int16_t *)0xE26F1A)

    /*
     * ROUTE_$N_ROUTING_PORTS - Current number of routing ports
     *
     * Original address: 0xE26F1C
     */
    #define ROUTE_$N_ROUTING_PORTS  (*(int16_t *)0xE26F1C)

    /*
     * ROUTE_$ROUTING - Routing table/flag
     *
     * Original address: 0xE26F1E
     */
    #define ROUTE_$ROUTING          (*(uint16_t *)0xE26F1E)

#else
    extern route_$port_t ROUTE_$PORT_ARRAY[];
    extern uint32_t ROUTE_$SOCK_ECVAL;
    extern route_$port_t *ROUTE_$PORTP_ARRAY[];
    extern ec_$eventcount_t *SOCK_$EVENT_COUNTERS[];
    extern uint32_t ROUTE_$SERVICE_MUTEX;
    extern uint32_t ROUTE_$CONTROL_ECVAL;
    extern uint32_t ROUTE_$CONTROL_EC;
    extern uint16_t ROUTE_$SOCK;
    extern int16_t ROUTE_$STD_N_ROUTING_PORTS;
    extern int16_t ROUTE_$N_ROUTING_PORTS;
    extern uint16_t ROUTE_$ROUTING;
#endif

/*
 * =============================================================================
 * Internal Function Prototypes
 * =============================================================================
 */

/* None currently defined - add as needed during implementation */

#endif /* ROUTE_INTERNAL_H */
