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
#include "ml/ml.h"   /* ml_$exclusion_t: rip_$data_t's three locks */

/* Forward declaration for opaque entry type */
struct rip_$entry_t;

/*
 * rip_$xns_addr_t - the 10-byte XNS route source address (network plus the
 * 6-byte host).  This is the shape of RIP_$UPDATE_D's / RIP_$UPDATE_INT's
 * "source" argument and of rip_$route_t.nexthop.
 *
 * Exported here (rather than from rip_internal.h) because route/ and
 * network/ build one on the stack to hand to RIP_$UPDATE_D / RIP_$UPDATE_INT
 * (ROUTE_$SERVICE 0x00E6A0E6, ROUTE_$CLOSE_PORT 0x00E69F5A).
 */
typedef struct rip_$xns_addr_t {
    uint32_t    network;        /* 0x00: Network address */
    uint8_t     host[6];        /* 0x04: Host address (6 bytes) */
} rip_$xns_addr_t;

/*
 * rip_$dest_addr_t - the 12-byte XNS destination RIP_$FIND_NEXTHOP is asked
 * about (network, 6-byte host, socket).  ROUTE_$PROCESS copies it straight
 * out of the IDP header at idp+6 (0x00E87564) and PKT_$BLD_INTERNET_HDR
 * builds one on its stack (0x00E12098).
 */
typedef struct rip_$dest_addr_t {
    uint32_t    network;        /* 0x00 */
    uint16_t    host_hi;        /* 0x04 */
    uint32_t    host_lo;        /* 0x06 */
    uint16_t    socket;         /* 0x0A */
} __attribute__((packed)) rip_$dest_addr_t;

/*
 * rip_$nexthop_t - the 10-byte answer RIP_$FIND_NEXTHOP writes back
 * (network plus the 6-byte host address); the socket is not part of it.
 * The three moves at 0x00E156BC / 0x00E1577C copy exactly 4 + 4 + 2 bytes.
 *
 * Callers that need the Apollo node id take the low longword of the host
 * address and mask it to 20 bits: (0x00E875EE) and (0x00E120CA) both do
 * "move.l #0xFFFFF,Dn; and.l (nexthop+6),Dn".
 */
typedef struct rip_$nexthop_t {
    uint32_t    network;        /* 0x00 */
    uint16_t    host_hi;        /* 0x04 */
    uint32_t    host_lo;        /* 0x06: low 20 bits are the node id */
} __attribute__((packed)) rip_$nexthop_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(rip_$dest_addr_t) == 12, "rip_$dest_addr_t must be 12 bytes");
_Static_assert(sizeof(rip_$nexthop_t) == 10, "rip_$nexthop_t must be 10 bytes");
_Static_assert(offsetof(rip_$nexthop_t, host_lo) == 6, "rip_$nexthop_t.host_lo");
#endif

/*
 * RIP_$STATS - RIP protocol statistics
 *
 * Located at 0xE262AC, tracks packet processing statistics.
 */
/*
 * The counter widths and offsets come from the instructions that touch them,
 * not from guesswork:
 *   0x00E68AD6  addq.l #0x1,(0x00E262AE).l   packets_received, a LONG at +0x02
 *   0x00E68B16  addq.w #0x1,(0x00E262B4).l   errors, a WORD at +0x08
 *   0x00E68DFC  addq.w #0x1,(0x00E262B6).l   unknown_commands, a WORD at +0x0A
 *   0x00E68E14  addq.w #0x1,(0x00E262B6).l   the same word
 * and ASKNODE_$INTERNET_INFO's request-0x41 arm reads the per-network packet
 * counters:
 *   0x00E6526E  move.l (0x00E262B8).l,(0x16,A1)      local network, +0x0C
 *   0x00E652F2  move.l (0x10,A4,D4*0x4),(0x16,A1)    A4 = RIP_$STATS,
 *                                                    D4 = the RIP_$INFO index
 * which makes the record 0x10 + 64*4 = 0x110 bytes, the size the older
 * comment here guessed at.
 */
typedef struct rip_$stats_t {
    uint16_t    _reserved0;         /* 0x00: Reserved */
    uint32_t    packets_received;   /* 0x02: Total packets received */
    uint16_t    _reserved1;         /* 0x06: Reserved */
    uint16_t    errors;             /* 0x08: Packet errors */
    uint16_t    unknown_commands;   /* 0x0A: Unknown command types */
    uint32_t    local_net_pkts;     /* 0x0C: packets for the local network */
    uint32_t    net_pkts[64];       /* 0x10: one counter per RIP_$INFO slot */
} __attribute__((packed)) rip_$stats_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(rip_$stats_t, packets_received) == 0x02, "rip_$stats_t.packets_received");
_Static_assert(offsetof(rip_$stats_t, errors)           == 0x08, "rip_$stats_t.errors");
_Static_assert(offsetof(rip_$stats_t, unknown_commands) == 0x0A, "rip_$stats_t.unknown_commands");
_Static_assert(offsetof(rip_$stats_t, local_net_pkts)   == 0x0C, "rip_$stats_t.local_net_pkts");
_Static_assert(offsetof(rip_$stats_t, net_pkts)         == 0x10, "rip_$stats_t.net_pkts");
_Static_assert(sizeof(rip_$stats_t) == 0x110, "rip_$stats_t must be 0x110 bytes");
#endif

/*
 * ============================================================================
 * Routing table (moved here from rip/rip_internal.h)
 * ============================================================================
 *
 * ASKNODE_$INTERNET_INFO's request-0x41 arm (0x00E6529C-0x00E65320) walks
 * RIP_$INFO itself, so the table constants and the entry layout have to be
 * reachable from outside the RIP subsystem.
 */

/* Number of entries in the routing table (hash table size) */
#define RIP_TABLE_SIZE          64
#define RIP_TABLE_MASK          0x3F

/* Route timeout value in clock ticks (360 = 6 minutes at 1 tick/sec) */
#define RIP_ROUTE_TIMEOUT       0x168

/* Route states (stored in top 2 bits of flags field) */
#define RIP_STATE_UNUSED        0   /* Slot is empty */
#define RIP_STATE_VALID         1   /* Route is active */
#define RIP_STATE_AGING         2   /* Route is being aged out */
#define RIP_STATE_EXPIRED       3   /* Route has expired */

#define RIP_STATE_SHIFT         6
#define RIP_STATE_MASK          0xC0

/* RIP infinity metric (unreachable) */
#define RIP_INFINITY            0x11

/* Number of route slots per entry (standard route + non-standard route) */
#define RIP_ROUTES_PER_ENTRY    2

/* Priority level for RIP lock */
#define RIP_LOCK_PRIORITY       0x0E

/*
 * Route entry structure (0x14 = 20 bytes)
 *
 * Holds routing information for reaching a network via a specific next hop.
 * Each routing table entry has two route slots: one for standard routes
 * and one for non-standard routes.
 */
typedef struct rip_$route_t {
    uint32_t            expiration;     /* 0x00: Expiration time (TIME_$CLOCKH ticks) */
    rip_$xns_addr_t     nexthop;        /* 0x04: Next hop address (10 bytes) */
    uint8_t             port;           /* 0x0E: Port number */
    uint8_t             metric;         /* 0x0F: Hop count (0x11 = infinity) */
    uint8_t             flags;          /* 0x10: state in bits 6-7.  This is a
                                         *       BYTE: every RIP function uses
                                         *       byte operations on it -
                                         *       "and.b (0x10,A2),D5b" with
                                         *       0xC0 at 0x00E155F8 and
                                         *       0x00E872E8, "andi.b #0x3f" at
                                         *       0x00E1562A, "ori.b #-0x80" /
                                         *       "ori.b #-0x40" at 0x00E15630 /
                                         *       0x00E15656 */
    uint8_t             _pad_11;        /* 0x11 */
    uint16_t            _pad_12;        /* 0x12: padding to 0x14 bytes */
} rip_$route_t;

/*
 * Routing table entry structure (0x2c = 44 bytes)
 *
 * Each entry represents a destination network with two possible routes:
 * - routes[0]: Standard route (for standard IDP traffic)
 * - routes[1]: Non-standard route (for non-standard traffic types)
 */
typedef struct rip_$entry_t {
    uint32_t        network;            /* 0x00: Destination network address */
    rip_$route_t    routes[RIP_ROUTES_PER_ENTRY]; /* 0x04: Route entries */
} rip_$entry_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(rip_$route_t, nexthop) == 0x04, "rip_$route_t.nexthop");
_Static_assert(offsetof(rip_$route_t, port)    == 0x0E, "rip_$route_t.port");
_Static_assert(offsetof(rip_$route_t, metric)  == 0x0F, "rip_$route_t.metric");
_Static_assert(offsetof(rip_$route_t, flags)   == 0x10, "rip_$route_t.flags");
_Static_assert(sizeof(rip_$route_t) == 0x14, "rip_$route_t must be 0x14 bytes");
_Static_assert(offsetof(rip_$entry_t, routes)  == 0x04, "rip_$entry_t.routes");
_Static_assert(sizeof(rip_$entry_t) == 0x2C, "rip_$entry_t must be 0x2C bytes");
#endif

/*
 * RIP subsystem data structure
 *
 * This is the main data block for the RIP subsystem, located at 0xE26258.
 * All offsets are relative to this base address.
 *
 * The structure contains:
 * - Routing port information at offset 0x00
 * - Three exclusion locks for different subsystems
 * - Routing table entries with reference counts
 * - Broadcast control parameters at offset 0xC68
 */
typedef struct rip_$data_t {
    uint32_t            route_port;         /* 0x00: Route port (set during diskless init) */
    uint8_t             _reserved0[0x0C];   /* 0x04: Reserved/unknown */
    ml_$exclusion_t     xns_error_mutex;    /* 0x10: XNS error client mutex (18 bytes) */
    uint8_t             _pad0[0x06];        /* 0x22: Padding to offset 0x28 */
    ml_$exclusion_t     route_service_mutex;/* 0x28: Route service mutex (18 bytes) */
    uint8_t             _pad0a[0x06];       /* 0x3A: Padding to offset 0x40 */
    ml_$exclusion_t     exclusion;          /* 0x40: RIP exclusion lock (18 bytes) */
    uint8_t             _pad1[0x0A];        /* 0x52: Padding to offset 0x5C */
    uint32_t            _reserved1;         /* 0x5C: Reserved */
    uint32_t            direct_hits;        /* 0x60: Direct route hit counter */
    uint32_t            ref_counts[RIP_TABLE_SIZE]; /* 0x64: Per-entry reference counts */
    rip_$entry_t        entries[RIP_TABLE_SIZE];    /* 0x164: Routing table entries */
    uint8_t             _reserved2[0x862];  /* Padding to 0xC68 */
    uint8_t             bcast_control[30];  /* 0xC68: Broadcast control params */
    uint8_t             _pad3[0x1C];        /* Padding to 0xC86 */
    uint8_t             std_recent_changes; /* 0xC86: Standard route changes flag */
    uint8_t             _pad4;              /* 0xC87: Padding */
    uint8_t             recent_changes;     /* 0xC88: Non-standard route changes flag */
} rip_$data_t;

extern rip_$data_t RIP_$DATA;
extern rip_$stats_t RIP_$STATS;

/*
 * RIP_$INFO - Base of the routing table entries (0xE263BC).
 * This is RIP_$DATA.entries (offset 0x164 of the RIP data block).
 */
#define RIP_$INFO               (RIP_$DATA.entries)


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
struct rip_$entry_t *RIP_$NET_LOOKUP(uint32_t network, boolean inc_refcount,
                                      boolean create_if_missing);

/*
 * RIP_$FIND_NEXTHOP - Find next hop for destination
 *
 * Looks up the routing table to find the next hop for a given destination.
 * First checks local ports (direct connections), then queries the routing table.
 *
 * @param addr_info     Destination address; the first 10 bytes (network plus
 *                      the 6-byte host) are read - see rip_$dest_addr_t
 * @param flags         If < 0, use non-standard routes; else use standard routes.
 *                      Read as a byte at (0xC,A6) at 0x00E156A8, i.e. a Pascal
 *                      boolean in the high half of its word slot.
 * @param port_ret      Output: port number for routing (a word: "move.w D2w,(A0)"
 *                      at 0x00E156B4; callers compare it against -1)
 * @param nexthop_ret   Output: rip_$nexthop_t, 10 bytes
 * @param status_ret    Output: status code (status_$ok or 0x3C0001 for no route)
 *
 * @return 0 on direct route (same network), non-zero metric on indirect route.
 *         The result is a word ("move.w D2w,D0w" at 0x00E1578C), not a byte.
 *
 * Original address: 0x00E15696
 */
int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
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
void RIP_$UPDATE_D(const uint32_t *network_ptr, void *source,
                   const uint16_t *hop_count_ptr, const uint8_t *port_info,
                   const boolean *flags_ptr, status_$t *status_ret);

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
void RIP_$TABLE_D(boolean *op_flag, boolean *route_type, uint16_t *index,
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
void RIP_$TABLE(boolean *op_flag, uint16_t *index, rip_$table_buf_t *buffer);

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
 * =============================================================================
 * Global Data shared with the ROUTE / XNS subsystems
 * =============================================================================
 *
 * These live in the RIP data block (0xE26258 ..).  On m68k they are accessed
 * at their absolute addresses; elsewhere they are variables in rip_data.c.
 */
#if defined(ARCH_M68K)
/* RIP_$STD_IDP_CHANNEL - IDP channel for RIP packets (0xFFFF = no channel) */
#define RIP_$STD_IDP_CHANNEL    (*(int16_t *)0xE26EBC)
/* RIP_$NS_ANNOUNCEMENT - Name service announcement data (2 bytes: 00 03) */
#define RIP_$NS_ANNOUNCEMENT    ((uint8_t *)0xE26EBE)
/* RIP_$BCAST_CONTROL - Broadcast control packet template (30 bytes) */
#define RIP_$BCAST_CONTROL      ((uint8_t *)0xE26EC0)
#else
extern int16_t RIP_$STD_IDP_CHANNEL;
extern uint8_t RIP_$NS_ANNOUNCEMENT[2];
extern uint8_t RIP_$BCAST_CONTROL[30];
#endif

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
void RIP_$PORT_CLOSE(uint16_t port_index, boolean flags, boolean force);

/*
 * RIP_$HALT_ROUTER - Gracefully stop the router
 *
 * @param flags  Route type to halt (a Pascal boolean read as a byte at
 *               (0x8,A6) by "move.b (0x8,A6),D0b" at 0x00E873A2):
 *                 If < 0: Halt non-standard routes
 *                 If >= 0: Halt standard routes
 *
 * Original address: 0x00E87396
 */
void RIP_$HALT_ROUTER(boolean flags);

/*
 * RIP_$UPDATE_INT - Internal route update
 *
 * Updates routing table entries with new route information.  Called both
 * inside RIP (RIP_$UPDATE_D / RIP_$SERVER) and from NETWORK_$FETCH_DISKLESS_INFO,
 * which is why it is declared here rather than in rip_internal.h.
 *
 * @param network      Network to update (-1 for all entries, 0 = no-op)
 * @param source       Source address (10 bytes, rip_$xns_addr_t)
 * @param hop_count    New hop count / metric (clamped to 17)
 * @param port_index   Port index for this route
 * @param flags        If < 0, use non-standard routes; else standard
 * @param status_ret   Output: status code
 *
 * Original address: 0x00E15922
 */
void RIP_$UPDATE_INT(uint32_t network, rip_$xns_addr_t *source,
                     uint16_t hop_count, uint16_t port_index,
                     boolean flags, status_$t *status_ret);

/*
 * RIP_$SEND_UPDATES - Send routing updates
 *
 * Sends routing update packets if there are recent changes.  ROUTE_$SERVICE
 * calls it directly (0x00E6A19C), so it is public.
 *
 * @param is_std    Pascal boolean read as a byte at (0x8,A6)
 *                  ("move.b (0x8,A6),D0b / bpl" at 0x00E6887E):
 *                  < 0 = non-standard routes, >= 0 = standard routes
 *
 * Original address: 0x00E6887A
 */
void RIP_$SEND_UPDATES(boolean is_std);

/*
 * RIP_$BROADCAST - Build and broadcast the full routing table
 *
 * ROUTE_$PROCESS calls this on its periodic timer (0x00E87470 / 0x00E8747C),
 * so it is public.
 *
 * @param flags     Pascal boolean read as a byte at (0x8,A6)
 *                  ("move.b (0x8,A6),D2b" at 0x00E872A6):
 *                  If < 0: broadcast non-standard routes (cap metric at 16)
 *                  If >= 0: broadcast standard routes
 *
 * Original address: 0x00E87298
 */
void RIP_$BROADCAST(boolean flags);

/*
 * RIP_$HALT_PACKET / RIP_$HALT_PACKET_DATA - the RIP "poison" packet the
 * router transmits when it shuts down: a 16-byte internet header followed by
 * 8 bytes of RIP data at +0x10.  The storage lives in the ROUTE wired data
 * area (route/route_data.c) but the only user is RIP_$HALT_ROUTER
 * (rip/misc.c, 0x00E873B4 / 0x00E873D6), so the declaration is public.
 *
 * Original addresses: 0xE87D68, 0xE87D78
 */
#if defined(ARCH_M68K)
#define RIP_$HALT_PACKET        ((uint8_t *)0xE87D68)
#define RIP_$HALT_PACKET_DATA   ((uint8_t *)0xE87D78)
#else
extern uint8_t RIP_$HALT_PACKET[24];
#define RIP_$HALT_PACKET_DATA   (&RIP_$HALT_PACKET[0x10])
#endif

/*
 * RIP_$SEND_DEST_ADDR - the destination-address scratch RIP_$SEND is handed
 *
 * The first 12 bytes of the same 0x00E87D68 block are a rip_$dest_addr_t that
 * RIP_$SEND rewrites in place (broadcast host and socket, then each port's
 * network in turn).  Both RIP_$BROADCAST ("lea (0xe87d68).l,A5" at
 * 0x00E872A0, "pea (A5)" at 0x00E87386) and RIP_$HALT_ROUTER (0x00E873B4)
 * pass it.  RIP_$SEND itself uses the same address as its A5 module base
 * (0x00E871BE).
 */
#define RIP_$SEND_DEST_ADDR     ((rip_$dest_addr_t *)RIP_$HALT_PACKET)

/*
 * Wired routing-send cells used by RIP_$SEND's nested procedure
 * RIP_$SEND_TO_PORT_INTERNET (rip/send.c).  Both addresses fall inside the
 * RIP_RTWIRED segments the SAU2 map names (I 0xE87000 size 0x3EC, D 0xE87D68
 * size 0x18), so RIP owns them; the storage is defined in route/route_data.c
 * (moved here from route/route.h -- bead source-3uo).
 *
 *   RTWIRED_$SEND_FLAGS  - send flags word at 0xE87D74 (A5+0xC, A5 = 0xE87D68)
 *   RTWIRED_$CALLBACK    - callback/data-length cell at 0xE870D8
 */
#if defined(ARCH_M68K)
#define RTWIRED_$SEND_FLAGS     (*(uint16_t *)0xE87D74)
#define RTWIRED_$CALLBACK       ((uint32_t *)0xE870D8)
#else
extern uint16_t RTWIRED_$SEND_FLAGS;
extern uint32_t RTWIRED_$CALLBACK_DATA;
#define RTWIRED_$CALLBACK       (&RTWIRED_$CALLBACK_DATA)
#endif


#endif /* RIP_H */
