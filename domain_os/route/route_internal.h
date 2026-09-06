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
#include "misc/crash_system.h"
#include "network/network.h"
#include "rip/rip.h"
#include "sock/sock.h"

/*
 * =============================================================================
 * Constants
 * =============================================================================
 */

/* Maximum number of routing ports */
#define ROUTE_MAX_PORTS 8

/* Port size in bytes */
#define ROUTE_PORT_SIZE 0x5C

/*
 * =============================================================================
 * Global Data (m68k addresses)
 * =============================================================================
 */

/* ROUTE_$PORT_ARRAY / ROUTE_$PORT: see route/route.h */

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
/* ROUTE_$SOCK, ROUTE_$STD_N_ROUTING_PORTS, ROUTE_$N_ROUTING_PORTS: see route/route.h */

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
void ROUTE_$DECREMENT_PORT(int8_t delete_flag, int16_t port_index,
                           int8_t port_type_flag);

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
 * Global Data in the Wired Routing Area (0xE87000 - 0xE88228)
 * =============================================================================
 *
 * The routing process code and its data live in a contiguous area that
 * route_$wire_routing_area() wires into physical memory.  On m68k these
 * are accessed at their absolute addresses (the A5-relative data of the
 * original Pascal module); on other architectures they are ordinary
 * variables defined in route_data.c.
 */

/* Maximum number of pages to wire for routing (constant at 0xE69BFC) */
#define ROUTE_$MAX_WIRED_PAGES  10

#if defined(ARCH_M68K)
/* Start/end of the wired routing area (pointers at 0xE69C04 / 0xE69C00) */
#define ROUTE_$WIRED_AREA_START ((void *)0x00E87000)
#define ROUTE_$WIRED_AREA_END   ((void *)0x00E88228)

/* RIP halt ("poison") packet: 16 byte header + 8 bytes of RIP data */
#define RIP_$HALT_PACKET        ((uint8_t *)0xE87D68)   /* 0xE87D68 */
#define RIP_$HALT_PACKET_DATA   ((uint8_t *)0xE87D78)   /* 0xE87D78 */

/* Global send flags word (A5+0xC where A5=0xE87D68), used by RTWIRED_PROC_START */
#define RTWIRED_$SEND_FLAGS     (*(uint16_t *)0xE87D74)

/* Array of wired page addresses */
#define ROUTE_$WIRED_PAGES      ((uint32_t *)0xE87D80)

/*
 * Routing statistics area (0x81 longwords, cleared by ROUTE_$INIT_ROUTING).
 * Entries 0..0x80 are per-packet-size counters; the named counters that
 * follow (0xE87FAC..) are cleared individually.
 */
#define ROUTE_$PACKET_STATS         ((uint32_t *)0xE87DA8)
#define ROUTE_$STAT_OVERSIZED_STD   (*(uint32_t *)0xE87FAC)
#define ROUTE_$STAT_DROPPED_STD_HOP (*(uint32_t *)0xE87FB0)
#define ROUTE_$STAT_DROPPED_STD_ROUTE (*(uint32_t *)0xE87FB4)
#define ROUTE_$STAT_FORWARDED_STD   (*(uint32_t *)0xE87FB8)
#define ROUTE_$STAT_OVERSIZED_N     (*(uint32_t *)0xE87FBC)
#define ROUTE_$STAT_DROPPED_N_HOP   (*(uint32_t *)0xE87FC0)
#define ROUTE_$STAT_DROPPED_N_ROUTE (*(uint32_t *)0xE87FC4)
#define ROUTE_$STAT_FORWARDED_N     (*(uint32_t *)0xE87FC8)
#define ROUTE_$USER_PORT_COUNT      (*(uint32_t *)0xE87FCC)
#define ROUTE_$USER_PORT_MAX        (*(uint16_t *)0xE87FD0)

/* Count of currently wired pages */
#define ROUTE_$N_WIRED_PAGES    (*(int16_t *)0xE87FD2)

/* Count of active user ports */
#define ROUTE_$N_USER_PORTS     (*(int16_t *)0xE87FD4)

/* Network service on/off operation codes and ring log id (in code segment) */
#define ROUTE_$NET_SERVICE_ON   (*(int16_t *)0xE8789C)
#define ROUTE_$NET_SERVICE_OFF  (*(int16_t *)0xE8789E)
#define RINGLOG_$ROUTE_FORWARD  (*(uint16_t *)0xE878A0)

/* Send callback/data pointer (4 bytes of zeros at 0xE870D8) */
#define RTWIRED_$CALLBACK       ((uint32_t *)0xE870D8)

/* Routing process state */
#define ROUTE_$PROCESS_UID      (*(uint16_t *)0xE88216)
#define ROUTE_$CHECKSUM_ENABLED (*(int8_t *)0xE88218)
#define ROUTE_$SERVICE_ID       (*(uint32_t *)0xE8821C)
#define PTR_ROUTE_$CONTROL_EC   (*(ec_$eventcount_t **)0xE88220)
#define ROUTE_$FWD_TIMEOUT      (*(uint16_t *)0xE88224)
#define ROUTE_$PACKET_SEQ       (*(uint16_t *)0xE88226)

/* Time of the last routing update (A5 base of the wired data) */
#define ROUTE_$LAST_UPDATE_TIME (*(uint32_t *)0xE825DC)
#else
extern char ROUTE_$WIRED_AREA_END_SYM[];
#define ROUTE_$WIRED_AREA_START ((void *)RTWIRED_PROC_START)
#define ROUTE_$WIRED_AREA_END   ((void *)ROUTE_$WIRED_AREA_END_SYM)

extern uint8_t RIP_$HALT_PACKET[24];
#define RIP_$HALT_PACKET_DATA   (&RIP_$HALT_PACKET[0x10])
extern uint16_t RTWIRED_$SEND_FLAGS;
extern uint32_t ROUTE_$WIRED_PAGES[ROUTE_$MAX_WIRED_PAGES];
extern uint32_t ROUTE_$PACKET_STATS[0x81];
extern uint32_t ROUTE_$STAT_OVERSIZED_STD;
extern uint32_t ROUTE_$STAT_DROPPED_STD_HOP;
extern uint32_t ROUTE_$STAT_DROPPED_STD_ROUTE;
extern uint32_t ROUTE_$STAT_FORWARDED_STD;
extern uint32_t ROUTE_$STAT_OVERSIZED_N;
extern uint32_t ROUTE_$STAT_DROPPED_N_HOP;
extern uint32_t ROUTE_$STAT_DROPPED_N_ROUTE;
extern uint32_t ROUTE_$STAT_FORWARDED_N;
extern uint32_t ROUTE_$USER_PORT_COUNT;
extern uint16_t ROUTE_$USER_PORT_MAX;
extern int16_t ROUTE_$N_WIRED_PAGES;
extern int16_t ROUTE_$N_USER_PORTS;
extern int16_t ROUTE_$NET_SERVICE_ON;
extern int16_t ROUTE_$NET_SERVICE_OFF;
extern uint16_t RINGLOG_$ROUTE_FORWARD;
extern uint32_t RTWIRED_$CALLBACK_DATA;
#define RTWIRED_$CALLBACK       (&RTWIRED_$CALLBACK_DATA)
extern uint16_t ROUTE_$PROCESS_UID;
extern int8_t ROUTE_$CHECKSUM_ENABLED;
extern uint32_t ROUTE_$SERVICE_ID;
extern ec_$eventcount_t *PTR_ROUTE_$CONTROL_EC;
extern uint16_t ROUTE_$FWD_TIMEOUT;
extern uint16_t ROUTE_$PACKET_SEQ;
extern uint32_t ROUTE_$LAST_UPDATE_TIME;
#endif

#endif /* ROUTE_INTERNAL_H */
