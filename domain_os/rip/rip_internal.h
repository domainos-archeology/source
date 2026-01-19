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
#else
    extern rip_$data_t RIP_$DATA;
    extern uint32_t TIME_$CLOCKH;
#endif

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
 * @param port          Port the update came from
 * @param rip_data_ptr  Pointer to RIP data structure
 * @param param3        Parameter (usually 0)
 * @param param4        Parameter (usually 0)
 * @param flags         If negative, use non-standard routes; else standard
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E15922
 */
void RIP_$UPDATE_INT(uint32_t port, void *rip_data_ptr, uint16_t param3,
                     uint16_t param4, int16_t flags, status_$t *status_ret);

/*
 * =============================================================================
 * External Global Variables (m68k addresses)
 * =============================================================================
 */

#if defined(M68K)
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
    extern int8_t NETWORK_$DISKLESS;
    extern uint32_t NETWORK_$MOTHER_NODE;
    extern uint32_t NODE_$ME;
    extern uint32_t ROUTE_$PORT;
    extern ec_$eventcount_t **SOCK_$EVENT_COUNTERS;
#endif

#endif /* RIP_INTERNAL_H */
