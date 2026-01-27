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
#include "rip/rip.h"
#include "sock/sock.h"
#include "misc/crash_system.h"

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


/*
 * ROUTE_$PORT_ARRAY - Array of routing port structures
 *
 * Array of 8 port structures, each 0x5C (92) bytes.
 * Total size: 8 * 92 = 736 bytes (0x2E0)
 *
 * Original address: 0xE2E0A0
 */
extern route_$port_t ROUTE_$PORT_ARRAY[];
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
extern uint32_t ROUTE_$SOCK_ECVAL;
/*
 * ROUTE_$SERVICE_MUTEX - Mutex for route service operations
 *
 * Original address: 0xE26280
 */
extern uint32_t ROUTE_$SERVICE_MUTEX;
/*
 * ROUTE_$CONTROL_ECVAL - Control event count value
 *
 * Original address: 0xE26F08
 */
extern uint32_t ROUTE_$CONTROL_ECVAL;
/*
 * ROUTE_$CONTROL_EC - Control event count
 *
 * Original address: 0xE26F0C
 */
extern uint32_t ROUTE_$CONTROL_EC;
/*
 * ROUTE_$SOCK - Socket reference
 *
 * Original address: 0xE26F18
 */
extern uint16_t ROUTE_$SOCK;
/*
 * ROUTE_$STD_N_ROUTING_PORTS - Standard number of routing ports
 *
 * Original address: 0xE26F1A
 */
extern int16_t ROUTE_$STD_N_ROUTING_PORTS;
/*
 * ROUTE_$N_ROUTING_PORTS - Current number of routing ports
 *
 * Original address: 0xE26F1C
 */
extern int16_t ROUTE_$N_ROUTING_PORTS;
/*
 * ROUTE_$ROUTING - Routing table/flag
 *
 * Original address: 0xE26F1E
 */
extern uint16_t ROUTE_$ROUTING;

/*
 * =============================================================================
 * Internal Function Prototypes
 * =============================================================================
 */

/*
 * ROUTE_$INIT_ROUTING - Initialize routing subsystem
 *
 * Called when routing is being enabled on a port. Increments the
 * appropriate port counter and initializes the routing subsystem
 * when the total routing ports reaches 2.
 *
 * @param port_index   Port index (0-7) being initialized
 * @param port_type    Port type flag: negative = increment STD counter,
 *                     non-negative = increment N counter
 *
 * Original address: 0x00E69CCC
 */
void ROUTE_$INIT_ROUTING(int16_t port_index, int8_t port_type);

/*
 * ROUTE_$CLOSE_PORT - Close and remove a routing port
 *
 * Called from ROUTE_$SERVICE when bit 3 (0x08) is set. Closes a
 * routing port, cleaning up all associated resources.
 *
 * @param port_info    Port information structure
 * @param status_ret   Output: status code
 *
 * Original address: 0x00E69EC2
 */
void ROUTE_$CLOSE_PORT(void *port_info, status_$t *status_ret);

/*
 * ROUTE_$DECREMENT_PORT - Decrement port counters during close
 *
 * Helper function that calls RIP_$PORT_CLOSE and decrements the
 * appropriate routing port counter. May halt the router if this
 * was the last active port.
 *
 * @param delete_flag      Delete notification flag
 * @param port_index       Port index being closed
 * @param port_type_flag   Port type flag (negative = STD)
 *
 * Original address: 0x00E69E40
 */
void ROUTE_$DECREMENT_PORT(int8_t delete_flag, int16_t port_index, int8_t port_type_flag);

/*
 * ROUTE_$CLEANUP_WIRED - Cleanup wired pages
 *
 * Unwires wired pages when there are no more user ports and
 * routing is not actively running.
 *
 * Original address: 0x00E69B7C
 */
void ROUTE_$CLEANUP_WIRED(void);

/*
 * route_$wire_routing_area - Wire routing memory area
 *
 * Wires the routing code pages in memory if not already wired.
 * Called during routing initialization to ensure routing code
 * is locked in physical memory.
 *
 * Original address: 0x00E69BCE
 */
void route_$wire_routing_area(void);

/*
 * ROUTE_$ANNOUNCE_NET - Announce network to mother node
 *
 * When running diskless, sends a broadcast control packet to the
 * mother node to announce this node's network address. Uses
 * PKT_$SEND_INTERNET to transmit the announcement.
 *
 * Only sends if NETWORK_$DISKLESS flag is negative (diskless mode).
 *
 * @param network   Network address to announce
 *
 * Original address: 0x00E69FB2
 */
void ROUTE_$ANNOUNCE_NET(uint32_t network);

/*
 * RTWIRED_PROC_START - Send RIP packet to wired/local port
 *
 * Sends a routing information protocol packet to a directly connected
 * (wired) network. Called from RIP_$SEND for ports that use the
 * internet layer rather than XNS/IDP routing.
 *
 * In the original Pascal implementation, this was a nested procedure
 * within RIP_$SEND that accessed the parent's stack frame. In this C
 * implementation, all necessary data is passed explicitly.
 *
 * The function:
 * 1. Allocates a network header buffer via NETWORK_$GETHDR
 * 2. Builds an internet header via PKT_$BLD_INTERNET_HDR
 * 3. Sends the packet via NET_IO_$SEND
 * 4. Returns the header buffer via NETWORK_$RTNHDR
 * 5. Advances the port's event counter if port is active
 *
 * @param port_index    Port index (0-7)
 * @param packet_id     Packet identifier (from PKT_$NEXT_ID)
 * @param route_data    Route data buffer (cmd + entries)
 * @param route_len     Route data length in bytes
 *
 * Original address: 0x00E87000
 */
void RTWIRED_PROC_START(int16_t port_index, uint16_t packet_id,
                        void *route_data, uint16_t route_len);

/*
 * =============================================================================
 * Additional Global Data for Wired Pages
 * =============================================================================
 */

#if defined(M68K)
    /*
     * ROUTE_$WIRED_PAGES - Array of wired page addresses
     *
     * Original address: 0xE87D80
     */
    #define ROUTE_$WIRED_PAGES      ((uint32_t *)0xE87D80)

    /*
     * ROUTE_$N_WIRED_PAGES - Count of currently wired pages
     *
     * Original address: 0xE87FD2
     */
    #define ROUTE_$N_WIRED_PAGES    (*(int16_t *)0xE87FD2)

    /*
     * ROUTE_$N_USER_PORTS - Count of active user ports
     *
     * Original address: 0xE87FD4
     */
    #define ROUTE_$N_USER_PORTS     (*(int16_t *)0xE87FD4)
#else
    extern uint32_t ROUTE_$WIRED_PAGES[];
    extern int16_t ROUTE_$N_WIRED_PAGES;
    extern int16_t ROUTE_$N_USER_PORTS;
#endif

#endif /* ROUTE_INTERNAL_H */
