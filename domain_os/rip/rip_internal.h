/*
 * RIP - Routing Information Protocol Internal Definitions
 *
 * Internal data structures and functions used only within the RIP subsystem.
 * External users should use rip.h instead.
 */

#ifndef RIP_INTERNAL_H
#define RIP_INTERNAL_H

#include "rip/rip.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "route/route.h"
#include "network/network.h"

/*
 * =============================================================================
 * Constants
 * =============================================================================
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
 * =============================================================================
 * Data Structures
 * =============================================================================
 */

/*
 * XNS network address (10 bytes)
 *
 * In XNS/IDP networking:
 * - Network: 4 bytes identifying the network
 * - Host: 6 bytes (typically Ethernet MAC address)
 */
typedef struct rip_$xns_addr_t {
    uint32_t    network;        /* 0x00: Network address */
    uint8_t     host[6];        /* 0x04: Host address (6 bytes) */
} rip_$xns_addr_t;

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
    uint16_t            flags;          /* 0x10: State flags (top 2 bits = state) */
    uint16_t            _pad;           /* 0x12: Padding to 0x14 bytes */
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

/*
 * =============================================================================
 * Global Data (m68k addresses)
 * =============================================================================
 */

#if defined(M68K)
    /* Main RIP data structure at 0xE26258 */
    #define RIP_$DATA               (*(rip_$data_t *)0xE26258)

    /* TIME_$CLOCKH - Current clock in ticks */
    #define TIME_$CLOCKH            (*(uint32_t *)0xE2B0D4)

    /* RIP_$INFO - Base of routing table entries (separate from RIP_$DATA.entries) */
    #define RIP_$INFO               ((rip_$entry_t *)0xE263BC)

    /* RIP_$STATS - Protocol statistics at 0xE262AC */
    #define RIP_$STATS              (*(rip_$stats_t *)0xE262AC)

    /* Routing port counts */
    #define ROUTE_$STD_N_ROUTING_PORTS  (*(int16_t *)0xE26F1A)
    #define ROUTE_$N_ROUTING_PORTS      (*(int16_t *)0xE26F1C)

    /* Recent changes flags (signed bytes - negative means changes pending) */
    #define RIP_$STD_RECENT_CHANGES     (*(int8_t *)0xE26EDE)
    #define RIP_$RECENT_CHANGES         (*(int8_t *)0xE26EE0)
#else
    extern rip_$data_t RIP_$DATA;
    extern uint32_t TIME_$CLOCKH;
    extern rip_$entry_t *RIP_$INFO;
    extern rip_$stats_t RIP_$STATS;
    extern int16_t ROUTE_$STD_N_ROUTING_PORTS;
    extern int16_t ROUTE_$N_ROUTING_PORTS;
    extern int8_t RIP_$STD_RECENT_CHANGES;
    extern int8_t RIP_$RECENT_CHANGES;
#endif

/* Status code for unknown network port */
#define status_$internet_unknown_network_port   0x2B0003

/* Status code for too many networks in internet (routing table full) */
#define status_$network_too_many_networks_in_internet   0x00110018

/*
 * =============================================================================
 * Internal Function Prototypes
 * =============================================================================
 */

/*
 * RIP_$LOCK - Acquire RIP subsystem lock
 *
 * Raises process priority and acquires the RIP exclusion lock.
 * Must be paired with RIP_$UNLOCK.
 *
 * Original address: 0x00E154A4
 */
void RIP_$LOCK(void);

/*
 * RIP_$UNLOCK - Release RIP subsystem lock
 *
 * Releases the RIP exclusion lock and restores process priority.
 *
 * Original address: 0x00E154C4
 */
void RIP_$UNLOCK(void);

/*
 * RIP_$AGE - Age routing table entries
 *
 * Scans all routing table entries and ages them:
 * - VALID routes past expiration become AGING
 * - AGING routes past expiration become EXPIRED (metric set to infinity)
 * - EXPIRED routes are cleared (UNUSED)
 *
 * After aging, calls RIP_$SEND_UPDATES to propagate changes.
 *
 * Original address: 0x00E155C0
 */
void RIP_$AGE(void);

/*
 * RIP_$SEND_UPDATES - Send routing updates
 *
 * Sends routing update packets if there are recent changes.
 *
 * @param is_std    Non-zero for standard routes, zero for non-standard
 *
 * Original address: 0x00E6887A
 */
void RIP_$SEND_UPDATES(int16_t is_std);

/*
 * RIP_$UPDATE_INT - Internal route update function
 *
 * Updates routing table entries with new route information.
 * Used during initialization and when receiving routing updates.
 *
 * The function supports two modes:
 * - Single network update: Updates a specific network entry
 * - Bulk update: Updates all entries matching the source (when network == -1)
 *
 * Update logic follows standard RIP rules:
 * 1. Accept updates from the same source (they might be withdrawing the route)
 * 2. Accept better routes (lower metric)
 * 3. Accept non-infinity updates to non-valid routes
 *
 * @param network      Network to update (-1 for all entries, 0 = no-op)
 * @param source       Pointer to source address (10 bytes XNS address)
 * @param hop_count    New hop count / metric (clamped to 17)
 * @param port_index   Port index for this route
 * @param flags        If negative, use non-standard routes; else standard
 * @param status_ret   Output: status code
 *
 * Original address: 0x00E15922
 */
void RIP_$UPDATE_INT(uint32_t network, rip_$xns_addr_t *source,
                     uint16_t hop_count, uint8_t port_index,
                     int8_t flags, status_$t *status_ret);

/*
 * Helper functions (nested Pascal procedures in original):
 *
 * RIP_$COMPARE_SOURCE (0x00E15830):
 *   Compares route source addresses. For non-standard routes, compares
 *   full 6-byte host address. For standard routes, compares lower 20 bits.
 *   Returns 0xFF (-1) if same source, 0 otherwise.
 *
 * RIP_$APPLY_UPDATE (0x00E15888):
 *   Applies update to a route entry. Handles route withdrawal (transition
 *   to AGING with short timeout) vs normal update (copy source, set VALID).
 *   Also sets recent_changes flag if metric changed.
 */

/* Route aging timeout (short) - used when route is being invalidated */
#define RIP_AGING_TIMEOUT       0x28

/*
 * RIP_$PACKET_LENGTH - Calculate RIP packet data length
 *
 * Returns the packet data length for a given number of route entries.
 * Each entry is 6 bytes (4 byte network + 2 byte metric) plus 2 bytes
 * for the command field.
 *
 * @param entry_count   Number of route entries
 * @return              Packet data length (entry_count * 6 + 2)
 *
 * Original address: 0x00E68864
 */
int16_t RIP_$PACKET_LENGTH(int16_t entry_count);

/*
 * RIP_$PROCESS_REQUEST - Process RIP request and build response
 *
 * Nested procedure called from RIP_$SERVER to process incoming RIP
 * requests. Builds a response containing route information.
 *
 * Two modes:
 * 1. Specific networks: Look up each requested network
 * 2. Full table: network=0xFFFFFFFF returns all VALID/AGING routes
 *
 * @param flags     If negative, use non-standard routes; else standard
 *
 * Note: This function accesses the caller's stack frame directly (nested
 * Pascal procedure pattern). In C, must be integrated into caller or
 * receive explicit buffer parameters.
 *
 * Original address: 0x00E688C8
 */
void RIP_$PROCESS_REQUEST(int8_t flags);

/*
 * RIP_$SERVER - Main RIP protocol server
 *
 * Processes incoming RIP packets from socket 8. Handles:
 * - Request (cmd=1): Send routing information for requested networks
 * - Response (cmd=2): Update routing table with received routes
 * - Name Register (cmd=3): Apollo extension for name service registration
 *
 * Called from socket receive processing when RIP packets arrive.
 *
 * Original address: 0x00E68A08
 */
uint16_t RIP_$SERVER(void);

/*
 * =============================================================================
 * Server Structures
 * =============================================================================
 */

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

/* RIP command types */
#define RIP_CMD_REQUEST         1
#define RIP_CMD_RESPONSE        2
#define RIP_CMD_NAME_REGISTER   3

/* Maximum entries per RIP packet */
#define RIP_MAX_ENTRIES         0x5A    /* 90 entries */

/* RIP packet entry size */
#define RIP_ENTRY_SIZE          6

/* RIP socket number */
#define RIP_SOCKET              8

/*
 * Table access types and functions are in rip.h (public API)
 * - rip_$table_d_buf_t
 * - rip_$table_buf_t
 * - RIP_$TABLE_D()
 * - RIP_$TABLE()
 */

/*
 * =============================================================================
 * Send/Broadcast Functions
 * =============================================================================
 */

/*
 * RIP_$SEND_TO_PORT - Send RIP packet to specific port via XNS/IDP
 *
 * Internal helper function that sends a RIP packet to a specific port
 * using the XNS/IDP protocol. Builds an IDP header with broadcast
 * destination, copies route data, and sends via XNS_IDP_$OS_SEND.
 *
 * @param port_index    Port index (0-7)
 * @param addr_info     Source address info (12 bytes)
 * @param route_data    Route data buffer (cmd + entries)
 * @param route_len     Route data length
 *
 * Original address: 0x00E870DC
 */
void RIP_$SEND_TO_PORT(int16_t port_index, void *addr_info,
                        void *route_data, uint16_t route_len);

/*
 * RTWIRED_PROC_START - Send RIP packet to wired/local port
 *
 * Sends a RIP packet to a directly connected (wired) network using
 * NET_IO_$SEND instead of IDP routing.
 *
 * In the original Pascal implementation, this was a nested procedure
 * that accessed the caller's stack frame. In C, all parameters are
 * passed explicitly.
 *
 * @param port_index    Port index (0-7)
 * @param packet_id     Packet identifier (from PKT_$NEXT_ID)
 * @param route_data    Route data buffer (cmd + entries)
 * @param route_len     Route data length in bytes
 *
 * Original address: 0x00E87000
 * Implemented in: route/rtwired_proc_start.c
 */
void RTWIRED_PROC_START(int16_t port_index, uint16_t packet_id,
                        void *route_data, uint16_t route_len);

/*
 * RIP_$SEND - Main RIP send function
 *
 * Sends a RIP packet to one or all ports. Dispatches to either
 * RIP_$SEND_TO_PORT (for XNS/IDP networks) or RTWIRED_PROC_START
 * (for wired/local networks) based on port flags.
 *
 * @param addr_info     Source address info (12 bytes: network + host + socket)
 * @param port_index    Port index (0-7), or -1 for all ports (broadcast)
 * @param route_data    Route data buffer (cmd + entries)
 * @param route_len     Route data length
 * @param flags         If < 0: non-standard routes, send via IDP only
 *                      If >= 0: standard routes, get new packet ID first
 *
 * Original address: 0x00E871B6
 */
void RIP_$SEND(void *addr_info, int16_t port_index, void *route_data,
               uint16_t route_len, int8_t flags);

/*
 * RIP_$BROADCAST - Build and broadcast full routing table
 *
 * Iterates through all routing table entries, builds a RIP response
 * packet containing all valid routes, and sends it to all ports.
 *
 * @param flags     If < 0: broadcast non-standard routes (cap metric at 16)
 *                  If >= 0: broadcast standard routes
 *
 * Original address: 0x00E87298
 */
void RIP_$BROADCAST(uint8_t flags);

/*
 * =============================================================================
 * Port Management Functions
 * =============================================================================
 */

/*
 * RIP_$STD_OPEN - Open standard RIP IDP channel
 *
 * Opens an XNS/IDP channel for receiving RIP packets on the standard
 * routing port. The channel uses RIP_$STD_DEMUX as its packet demultiplexer.
 *
 * On success, the channel number is stored in RIP_$STD_IDP_CHANNEL.
 *
 * Original address: 0x00E15AAE
 */
void RIP_$STD_OPEN(void);

/*
 * RIP_$STD_DEMUX - Demultiplex incoming RIP packets
 *
 * Demultiplexer callback invoked by XNS_IDP when a packet arrives on
 * the RIP channel. Extracts relevant information from the IDP packet
 * and queues it for the RIP server via SOCK_$PUT on socket 8.
 *
 * Parameters:
 * @param pkt           Pointer to IDP packet structure
 * @param param_2       Pointer to event count parameter 1
 * @param param_3       Pointer to event count parameter 2
 * @param param_4       Unused
 * @param status_ret    Output: status code (0x3B0016 on success)
 *
 * Original address: 0x00E15A2C
 */
void RIP_$STD_DEMUX(void *pkt, uint16_t *param_2, uint16_t *param_3,
                    void *param_4, status_$t *status_ret);

/*
 * RIP_$PORT_CLOSE - Invalidate routes through a closing port
 *
 * When a port is being closed, marks all routes using that port as
 * expired. Sets metric to infinity (0x11), state to EXPIRED, and
 * triggers route update broadcasts.
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
 * =============================================================================
 * Miscellaneous Functions
 * =============================================================================
 */

/*
 * RIP_$ANNOUNCE_NS is declared in rip.h (public API)
 */

/*
 * RIP_$HALT_ROUTER - Gracefully stop the router
 *
 * Called when the number of routing ports drops to 1, indicating that
 * the router should stop advertising routes. Sends a "poison" RIP
 * response to all neighbors indicating all routes through this router
 * are now unreachable (metric = 16).
 *
 * @param flags  Route type to halt:
 *                 If < 0: Halt non-standard routes (send via IDP)
 *                 If >= 0: Halt standard routes (send via wired)
 *
 * The function also clears the appropriate recent_changes flag:
 * - flags < 0: clears RIP_$STD_RECENT_CHANGES
 * - flags >= 0: clears RIP_$RECENT_CHANGES
 *
 * Original address: 0x00E87396
 */
void RIP_$HALT_ROUTER(int16_t flags);

/*
 * =============================================================================
 * External Global Variables (m68k addresses)
 * =============================================================================
 */

#if defined(M68K)
    /* RIP_$STD_IDP_CHANNEL - IDP channel for RIP packets (0xFFFF = no channel) */
    #define RIP_$STD_IDP_CHANNEL    (*(int16_t *)0xE26EBC)

    /* NETWORK_$DISKLESS - Diskless boot flag (bit 7 set = diskless) */
    #define NETWORK_$DISKLESS       (*(int8_t *)0xE24C4C)

    /* NETWORK_$MOTHER_NODE - Node ID of boot server */
    #define NETWORK_$MOTHER_NODE    (*(uint32_t *)0xE24C0C)

    /* NODE_$ME - This node's ID */
    #define NODE_$ME                (*(uint32_t *)0xE245A4)

    /* ROUTE_$PORT - Primary routing port */
    #define ROUTE_$PORT             (*(uint32_t *)0xE2E0A0)

    /* Socket event counter array (indexed by socket number) */
    #define SOCK_$EVENT_COUNTERS    ((ec_$eventcount_t **)0xE28DB4)
#else
    extern int16_t RIP_$STD_IDP_CHANNEL;
    extern int8_t NETWORK_$DISKLESS;
    extern uint32_t NETWORK_$MOTHER_NODE;
    extern uint32_t NODE_$ME;
    extern uint32_t ROUTE_$PORT;
    extern ec_$eventcount_t **SOCK_$EVENT_COUNTERS;
#endif

#endif /* RIP_INTERNAL_H */
