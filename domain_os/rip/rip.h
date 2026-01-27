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
 * RIP_$STATS - RIP protocol statistics
 *
 * Located at 0xE262AC, tracks packet processing statistics.
 */
typedef struct rip_$stats_t {
    uint16_t    _reserved0;         /* 0x00: Reserved */
    uint32_t    packets_received;   /* 0x02: Total packets received */
    uint16_t    _reserved1;         /* 0x06: Reserved */
    uint32_t    errors;             /* 0x08: Packet errors */
    uint16_t    unknown_commands;   /* 0x0C: Unknown command types */
    /* ... more fields follow to ~0x110 bytes */
} rip_$stats_t;

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
 * =============================================================================
 * Table Access Data Structures
 * =============================================================================
 */

/*
 * RIP_$TABLE_D buffer format (26 bytes)
 *
 * This is the data format used by RIP_$TABLE_D for reading/writing
 * routing table entries. It contains both the route info and port
 * identification info.
 */
typedef struct rip_$table_d_buf_t {
    uint32_t    expiration;         /* 0x00: Route expiration time */
    uint32_t    dest_network;       /* 0x04: Destination network address */
    uint32_t    nexthop_network;    /* 0x08: Next hop network address */
    uint8_t     nexthop_host[6];    /* 0x0C: Next hop host address (6 bytes) */
    uint16_t    port_network;       /* 0x12: Port network identifier */
    uint16_t    port_socket;        /* 0x14: Port socket identifier */
    uint16_t    metric;             /* 0x16: Route metric (hop count) */
    uint16_t    state;              /* 0x18: Route state (0-3) */
} rip_$table_d_buf_t;

/*
 * RIP_$TABLE buffer format (16 bytes)
 *
 * This is the compact data format used by RIP_$TABLE for external access.
 * It provides a simpler interface for reading/writing table entries.
 */
typedef struct rip_$table_buf_t {
    uint32_t    dest_network;       /* 0x00: Destination network address */
    uint32_t    nexthop_host_low;   /* 0x04: Lower 20 bits of nexthop host */
    uint32_t    expiration;         /* 0x08: Route expiration time */
    uint8_t     port_index;         /* 0x0C: Port index (0-7) */
    uint8_t     metric;             /* 0x0D: Route metric (hop count) */
    uint8_t     state_flags;        /* 0x0E: State in upper 2 bits */
    uint8_t     _pad;               /* 0x0F: Padding */
} rip_$table_buf_t;

/*
 * =============================================================================
 * Syscall Functions
 * =============================================================================
 */

/*
 * RIP_$TABLE_D - Direct table entry access
 *
 * Reads or writes a routing table entry with full detail, including
 * port network and socket identification.
 *
 * @param op_flag       If *op_flag < 0, read; else write
 * @param route_type    If *route_type < 0, non-standard route; else standard route
 * @param index         Pointer to entry index (0-63, will be masked)
 * @param buffer        Pointer to rip_$table_d_buf_t for data transfer
 * @param status_ret    Output: status code
 *
 * For reads:
 *   Copies the entry to buffer, including port network/socket info
 *
 * For writes:
 *   Uses ROUTE_$FIND_PORT to map port_network/port_socket to port index,
 *   then writes the entry. Returns status_$internet_unknown_network_port
 *   (0x2B0003) if the port cannot be found.
 *
 * Original address: 0x00E68E2C
 */
void RIP_$TABLE_D(int8_t *op_flag, int8_t *route_type, uint16_t *index,
                  rip_$table_d_buf_t *buffer, status_$t *status_ret);

/*
 * RIP_$TABLE - Simplified table entry access
 *
 * Reads or writes a routing table entry using a compact format.
 * Always accesses standard routes (route_type = 0).
 *
 * @param op_flag       If *op_flag < 0, read; else write
 * @param index         Entry index
 * @param buffer        Pointer to rip_$table_buf_t for data transfer
 *
 * For reads:
 *   Reads the entry and reformats into compact buffer format.
 *
 * For writes:
 *   Only accepts port_index < 8. Uses port_index to look up port info,
 *   then writes via TABLE_D.
 *
 * Original address: 0x00E68F90
 */
void RIP_$TABLE(int8_t *op_flag, uint16_t *index, rip_$table_buf_t *buffer);

/*
 * RIP_$ANNOUNCE_NS - Announce name service availability via RIP
 *
 * Registers the routing port with the remote name service and broadcasts
 * a name service announcement packet to all nodes on the network.
 *
 * This function:
 * 1. Calls REM_NAME_$REGISTER_SERVER to register with name service
 * 2. Gets a unique packet ID via PKT_$NEXT_ID
 * 3. Broadcasts announcement via PKT_$SEND_INTERNET on socket 8
 *
 * Original address: 0x00E6914E
 */
void RIP_$ANNOUNCE_NS(void);

/*
 * Status codes
 */
#define RIP_$STATUS_NO_ROUTE    0x3C0001    /* No route to destination */

/*
 * RIP_$PORT_CLOSE - Invalidate routes through a closing port
 *
 * @param port_index    Port index (0-7) being closed
 * @param flags         If < 0, process non-standard routes; else standard
 * @param force         If < 0, invalidate all routes on port;
 *                      If >= 0, only invalidate routes with non-zero metric
 *
 * Original address: 0x00E15798
 */
void RIP_$PORT_CLOSE(uint16_t port_index, int8_t flags, int8_t force);

/*
 * RIP_$HALT_ROUTER - Gracefully stop the router
 *
 * @param flags  Route type to halt:
 *                 If < 0: Halt non-standard routes
 *                 If >= 0: Halt standard routes
 *
 * Original address: 0x00E87396
 */
void RIP_$HALT_ROUTER(int16_t flags);

#endif /* RIP_H */
