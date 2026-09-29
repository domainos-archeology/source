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
 * =============================================================================
 * RIP_$WIRED_DATA - the RIP_WIRED module data block (0xE26258, 0xC8C bytes)
 * =============================================================================
 *
 * Module data blocks RIP_$WIRED_DATA, RIP_$INIT_DATA and RIP_$RTWIRED_DATA:
 * Claude Opus 5.5 (source-thww).
 *
 * Map "D E26258 RIP_WIRED size = C8C".  The A5 block of the RIP_WIRED code
 * ("lea (0xe26258).l,A5" in RIP_$NET_LOOKUP 0x00E154EC and
 * RIP_$FIND_NEXTHOP 0x00E1569E); RIP_$INIT reaches it through the literal
 * base instead ("movea.l #0xe26258,A0 / pea (0x40,A0)", 0x00E2FBDE) and
 * other modules through absolute cells.  It also holds the two locks the
 * map exports for XNS_ERROR and ROUTE, which RIP_$INIT initialises together
 * with its own (0x00E2FBE4, 0x00E2FBF6, 0x00E2FC08).  Every map symbol in the
 * segment is a field here:
 *
 *   +0x000  (route port)               RIP_$INIT 0x00E2FD6A; also read as
 *                                      the rip_$xns_addr_t source of its two
 *                                      RIP_$UPDATE_INT calls (0x00E2FD72)
 *   +0x010  XNS_ERROR_$CLIENT_MUTEX    0xE26268
 *   +0x028  ROUTE_$SERVICE_MUTEX       0xE26280 ("move.l #0xe26280,-(SP)"
 *                                      in ROUTE_$SERVICE, 0x00E6A048)
 *   +0x040  (RIP exclusion lock)       RIP_$LOCK / RIP_$UNLOCK
 *   +0x054  RIP_$STATS                 0xE262AC, rip_$stats_t (0x110)
 *   +0x164  RIP_$INFO                  0xE263BC, 64 entries of 0x2C
 *   +0xC64  RIP_$STD_IDP_CHANNEL       0xE26EBC, -1 = no channel
 *   +0xC66  RIP_$NS_ANNOUNCEMENT       0xE26EBE, the 2-byte template 00 03
 *   +0xC68  RIP_$BCAST_CONTROL         0xE26EC0, a 30-byte pkt_$info_t
 *   +0xC86  RIP_$STD_RECENT_CHANGES    0xE26EDE, Pascal boolean
 *   +0xC88  RIP_$RECENT_CHANGES        0xE26EE0, Pascal boolean
 *
 * RIP_$STATS's last two fields are the counters RIP_$FIND_NEXTHOP and
 * RIP_$NET_LOOKUP bump: stats.local_net_pkts (+0x060) and stats.net_pkts[i]
 * (+0x064 + i*4, one per RIP_$INFO slot, indexed with the 0-based slot) -
 * the same cells ASKNODE_$INTERNET_INFO reports (0x00E6526E, 0x00E652F2).
 *
 * The exclusion locks hold pointers, so every offset past +0x10 is asserted
 * on the target only.
 */
#define RIP_$WIRED_DATA_SIZE    0xC8C   /* map: RIP_WIRED size = C8C */

typedef struct rip_$wired_data_t {
    uint32_t            route_port;         /* +0x000: the node's network,
                                             *         read by RIP_$INIT's
                                             *         RIP_$UPDATE_INT calls as
                                             *         a rip_$xns_addr_t */
    uint8_t             _0004[0x0C];        /* +0x004: the source address's
                                             *         host half, never
                                             *         written */
    ml_$exclusion_t     xns_error_mutex;    /* +0x010 map XNS_ERROR_$CLIENT_MUTEX */
    uint8_t             _0022[0x06];        /* +0x022 */
    ml_$exclusion_t     route_service_mutex;/* +0x028 map ROUTE_$SERVICE_MUTEX */
    uint8_t             _003a[0x06];        /* +0x03A */
    ml_$exclusion_t     exclusion;          /* +0x040: the RIP lock */
    uint8_t             _0052[0x02];        /* +0x052 */
    rip_$stats_t        stats;              /* +0x054 map RIP_$STATS */
    rip_$entry_t        info[RIP_TABLE_SIZE];   /* +0x164 map RIP_$INFO, [0..63] */
    int16_t             std_idp_channel;    /* +0xC64 map RIP_$STD_IDP_CHANNEL */
    uint8_t             ns_announcement[2]; /* +0xC66 map RIP_$NS_ANNOUNCEMENT */
    uint8_t             bcast_control[30];  /* +0xC68 map RIP_$BCAST_CONTROL:
                                             *         a pkt_$info_t, kept in
                                             *         bytes because it is
                                             *         copied and passed by
                                             *         address, never read
                                             *         field by field here */
    int8_t              std_recent_changes; /* +0xC86 map RIP_$STD_RECENT_CHANGES */
    uint8_t             _0c87;              /* +0xC87 */
    int8_t              recent_changes;     /* +0xC88 map RIP_$RECENT_CHANGES */
    uint8_t             _0c89[3];           /* +0xC89 */
} rip_$wired_data_t;

_Static_assert(offsetof(rip_$wired_data_t, route_port) == 0x000, "RIP_WIRED route port");
_Static_assert(offsetof(rip_$wired_data_t, xns_error_mutex) == 0x010,
               "XNS_ERROR_$CLIENT_MUTEX (0xE26268)");
#if defined(ARCH_M68K)
/* Pointer-bearing records from +0x10 on: target-only (design section 3). */
_Static_assert(offsetof(rip_$wired_data_t, route_service_mutex) == 0x028,
               "ROUTE_$SERVICE_MUTEX (0xE26280)");
_Static_assert(offsetof(rip_$wired_data_t, exclusion) == 0x040, "RIP lock (pea (0x40,A0))");
_Static_assert(offsetof(rip_$wired_data_t, stats) == 0x054, "RIP_$STATS (0xE262AC)");
_Static_assert(offsetof(rip_$wired_data_t, stats.local_net_pkts) == 0x060,
               "RIP_$STATS.local_net_pkts (0xE262B8)");
_Static_assert(offsetof(rip_$wired_data_t, stats.net_pkts) == 0x064,
               "RIP_$STATS.net_pkts (0xE262BC)");
_Static_assert(offsetof(rip_$wired_data_t, info) == 0x164, "RIP_$INFO (0xE263BC)");
_Static_assert(sizeof(((rip_$wired_data_t *)0)->info[0]) == 0x2C, "RIP_$INFO stride 0x2C");
_Static_assert(offsetof(rip_$wired_data_t, std_idp_channel) == 0xC64,
               "RIP_$STD_IDP_CHANNEL (0xE26EBC)");
_Static_assert(offsetof(rip_$wired_data_t, ns_announcement) == 0xC66,
               "RIP_$NS_ANNOUNCEMENT (0xE26EBE)");
_Static_assert(offsetof(rip_$wired_data_t, bcast_control) == 0xC68,
               "RIP_$BCAST_CONTROL (0xE26EC0)");
_Static_assert(offsetof(rip_$wired_data_t, std_recent_changes) == 0xC86,
               "RIP_$STD_RECENT_CHANGES (0xE26EDE)");
_Static_assert(offsetof(rip_$wired_data_t, recent_changes) == 0xC88,
               "RIP_$RECENT_CHANGES (0xE26EE0)");
_Static_assert(sizeof(rip_$wired_data_t) == RIP_$WIRED_DATA_SIZE, "RIP_WIRED: map size 0xC8C");
#endif

MODULE_DATA_DECLARE(rip_$wired_data_t, RIP_$WIRED_DATA, 0x00E26258);

/*
 * =============================================================================
 * RIP_$INIT_DATA - RIP_$INIT's A5 block (0xE3502C, 4 bytes)
 * =============================================================================
 *
 * Map "D E3502C RIP_WIRED size = 4", inside OS_INIT_DATA with no interior
 * symbol.  RIP_$INIT loads it into A5 ("lea (0xe3502c).l,A5" at 0x00E2FBD8)
 * and passes it as the 2-byte request template of its PKT_$SEND_INTERNET
 * call ("pea (A5)" at 0x00E2FCA6, template length 2).  RING_$INIT loads the
 * same address into A5 (0x00E2FAE8) and never uses it.  All four bytes are
 * zero in the image.
 */
#define RIP_$INIT_DATA_SIZE     4       /* map: RIP_WIRED size = 4 */

typedef struct rip_$init_data_t {
    uint16_t    request;                /* +0x00: the template, 0 */
    uint16_t    _02;                    /* +0x02: not referenced */
} rip_$init_data_t;

_Static_assert(offsetof(rip_$init_data_t, request) == 0x00, "RIP_$INIT template (pea (A5))");
_Static_assert(sizeof(rip_$init_data_t) == RIP_$INIT_DATA_SIZE, "RIP_WIRED (0xE3502C): map size 4");

MODULE_DATA_DECLARE(rip_$init_data_t, RIP_$INIT_DATA, 0x00E3502C);



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
 * =============================================================================
 * RIP_$RTWIRED_DATA - the RIP_RTWIRED module data block (0xE87D68, 0x18)
 * =============================================================================
 *
 * Map "D E87D68 RIP_RTWIRED size = 18", the first block of the wired routing
 * data (RTWIRED_DATA_START); ROUTE_$RTWIRED_DATA follows it.  RIP_$SEND,
 * RIP_$BROADCAST and RIP_$HALT_ROUTER all load it into A5
 * ("lea (0xe87d68).l,A5" at 0x00E871BE, 0x00E872A0, 0x00E8739C):
 *
 *   +0x00  dest_addr   the rip_$dest_addr_t RIP_$SEND is handed ("pea (A5)"
 *                      at 0x00E87386 and 0x00E873B4) and rewrites in place
 *                      when it sends to every port
 *   +0x0C  send_flags  the NET_IO_$SEND flags word of the Domain internet
 *                      transmit ("move.w (0xc,A5),-(SP)" at 0x00E8708C,
 *                      RIP_$SEND_TO_PORT_INTERNET inheriting RIP_$SEND's A5)
 *   +0x10  halt_data   the 8-byte RIP response RIP_$HALT_ROUTER sends when
 *                      the router stops ("pea (0x10,A5)"): command 2,
 *                      network 0xFFFFFFFF, metric 16.  Kept in bytes: it is
 *                      packet payload, copied out as it stands.
 *
 * Pointer-free, so every assert is unconditional.
 */
#define RIP_$RTWIRED_DATA_SIZE  0x18    /* map: RIP_RTWIRED size = 18 */

typedef struct rip_$rtwired_data_t {
    rip_$dest_addr_t    dest_addr;      /* +0x00 */
    uint16_t            send_flags;     /* +0x0C */
    uint16_t            _0e;            /* +0x0E: not referenced */
    uint8_t             halt_data[8];   /* +0x10 */
} rip_$rtwired_data_t;

_Static_assert(offsetof(rip_$rtwired_data_t, dest_addr) == 0x00, "RIP_$SEND destination (pea (A5))");
_Static_assert(offsetof(rip_$rtwired_data_t, send_flags) == 0x0C, "send flags (0xc,A5)");
_Static_assert(offsetof(rip_$rtwired_data_t, halt_data) == 0x10, "halt packet data (0x10,A5)");
_Static_assert(sizeof(rip_$rtwired_data_t) == RIP_$RTWIRED_DATA_SIZE, "RIP_RTWIRED: map size 0x18");

MODULE_DATA_DECLARE(rip_$rtwired_data_t, RIP_$RTWIRED_DATA, 0x00E87D68);


#endif /* RIP_H */
