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
#include "rip/rip.h"   /* rip_$dest_addr_t: route_$port_t.xns_addr */

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
/*
 * route_$driver_info_t - the per-port driver record route_$port_t.driver_info
 * (+0x48) points at.  Only two fields have been recovered so far.
 */
typedef struct route_$driver_info_t {
    uint16_t    _unknown0;      /* 0x00 */
    uint16_t    max_data_len;   /* 0x02: largest data length this port will
                                 *       carry.  PKT_$BLD_INTERNET_HDR compares
                                 *       the data length against it and then
                                 *       against it plus 0x100 together with
                                 *       the template ("cmp.w (0x2,A0),D3w" at
                                 *       0x00E1211E and "move.w (0x2,A0),D6w /
                                 *       addi.l #0x100,D6" at 0x00E12136);
                                 *       MSG_$$SEND repeats both tests at
                                 *       0x00E0DAD0 and 0x00E0DAEC. */
    uint16_t    _unknown4;      /* 0x04 */
    uint8_t     _unknown6;      /* 0x06 */
    uint8_t     flags;          /* 0x07: ROUTE_$VALIDATE_PORT reads this byte */
    uint8_t     _unknown8[0x1C];/* 0x08 */
    uint32_t    set_service;    /* 0x24: driver entry point NETWORK_$SET_SERVICE
                                 *       calls after every successful update
                                 *       ("movea.l (0x24,A1),A0 / jsr (A0)" at
                                 *       0x00E0F5E4).  Held as a target VA, so
                                 *       reach it with ARCH_VA_TO_PTR. */
} route_$driver_info_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(route_$driver_info_t, max_data_len) == 0x02,
               "route_$driver_info_t.max_data_len");
_Static_assert(offsetof(route_$driver_info_t, flags) == 0x07,
               "route_$driver_info_t.flags");
_Static_assert(offsetof(route_$driver_info_t, set_service) == 0x24,
               "route_$driver_info_t.set_service");
#endif

/*
 * route_$set_service_fn_t - the driver entry at route_$driver_info_t+0x24
 *
 * NETWORK_$SET_SERVICE (0x00E0F5CA) pushes, right to left:
 *   subq.l #0x2,SP            ; word result slot; the result is discarded
 *   pea (-0x90,A6)            ; arg 5, an uninitialised local
 *   pea (-0x9a,A6)            ; arg 4, an uninitialised local
 *   move.w #0x88,-(SP)        ; arg 3, by value
 *   pea (-0x88,A6)            ; arg 2, the two-word record {0, service}
 *   pea (0xe2e0d0).l          ; arg 1, &ROUTE_$PORT_ARRAY[0].socket
 * The types of arguments 4 and 5 are unproven: the caller neither
 * initialises nor reads them.
 */
typedef int16_t (*route_$set_service_fn_t)(uint16_t *socket_ptr,
                                           const uint16_t *service_rec,
                                           uint16_t request,
                                           void *out4, void *out5);

typedef struct route_$port_t {
    uint32_t    network;            /* 0x00: Network address */
    uint8_t     _unknown0[0x1C];    /* 0x04: Unknown fields */
    rip_$dest_addr_t xns_addr;      /* 0x20: this port's own XNS endpoint
                                     *       {network, 6-byte host, socket}.
                                     *       RIP_$SEND_TO_PORT copies all 12
                                     *       bytes into the IDP header's source
                                     *       field ("lea (0x20,A3),A1" at
                                     *       0x00E87134) and then overwrites
                                     *       the socket half with 1. */
    uint16_t    active;             /* 0x2C: routing-capability BIT NUMBER;
                                     *       tested with "btst.l D0,D1" against
                                     *       0x28 (standard) and 0x30
                                     *       (non-standard) at 0x00E87240 /
                                     *       0x00E87258 */
    uint16_t    port_type;          /* 0x2E: Port type (1=local, 2=routing) */
    uint16_t    socket;             /* 0x30: Socket identifier */
    uint8_t     _unknown1[0x04];    /* 0x32: Unknown fields */
    uint16_t    socket2;            /* 0x36: Secondary socket */
    uint8_t     port_ec[0x0C];      /* 0x38: Port event count (ec_$eventcount_t, 12 bytes) */
    uint32_t    driver_stats;       /* 0x44: Driver statistics block pointer (32-bit
                                     *       address; ROUTE_$SEND_USER_PORT:
                                     *       movea.l (0x44,A0,D0),A2) */
    uint32_t    driver_info;        /* 0x48: route_$driver_info_t * as a 32-bit
                                     *       target address (same treatment as
                                     *       driver_stats above), reached with
                                     *       ARCH_VA_TO_PTR */
    uint8_t     _unknown2[0x0C];    /* 0x4C: Unknown fields */
    uint32_t    forward_count;      /* 0x58: Packets forwarded to this port (ROUTE_$PROCESS) */
} route_$port_t;

/* Port entry size must match the original 0x5C-byte stride */
#if defined(ARCH_M68K)
_Static_assert(offsetof(route_$port_t, network)       == 0x00, "route_$port_t.network");
_Static_assert(offsetof(route_$port_t, xns_addr)      == 0x20, "route_$port_t.xns_addr");
_Static_assert(offsetof(route_$port_t, active)        == 0x2C, "route_$port_t.active");
_Static_assert(offsetof(route_$port_t, port_type)     == 0x2E, "route_$port_t.port_type");
_Static_assert(offsetof(route_$port_t, socket)        == 0x30, "route_$port_t.socket");
_Static_assert(offsetof(route_$port_t, socket2)       == 0x36, "route_$port_t.socket2");
_Static_assert(offsetof(route_$port_t, port_ec)       == 0x38, "route_$port_t.port_ec");
_Static_assert(offsetof(route_$port_t, driver_stats)  == 0x44, "route_$port_t.driver_stats");
_Static_assert(offsetof(route_$port_t, driver_info)   == 0x48, "route_$port_t.driver_info");
_Static_assert(offsetof(route_$port_t, forward_count) == 0x58, "route_$port_t.forward_count");
_Static_assert(sizeof(route_$port_t) == 0x5C, "route_$port_t must be 0x5C bytes");
#endif

/*
 * route_$port_stats_t - the statistics block a routing port points at
 *
 * route_$port_t.driver_stats (+0x44) holds the address of this block.
 * ROUTE_$PROCESS updates it after handing a forwarded packet to a user
 * routing port (0x00E87618 - 0x00E87664) and ROUTE_$READ_USER_STATS copies
 * the first ten bytes plus a port-dependent number of queue-depth buckets
 * out to the caller (0x00E6A6xx).
 *
 * The longwords sit on odd-numbered word boundaries (0x02, 0x06, 0x0A+n*4),
 * so the record has to be packed to lay out the same way off m68k.
 */
typedef struct route_$port_stats_t {
    uint16_t    flags;              /* 0x00: byte 0 is copied out by
                                     *       ROUTE_$READ_USER_STATS */
    uint32_t    deep_queue_puts;    /* 0x02: SOCK_$PUT succeeded with a socket
                                     *       queue depth above 0x20
                                     *       (addq.l #1,(0x2,A2) at 0xE8764E) */
    uint32_t    failed_puts;        /* 0x06: SOCK_$PUT failed
                                     *       (addq.l #1,(0x6,A2) at 0xE87660) */
    uint32_t    queue_depth[0x21];  /* 0x0A: SOCK_$PUT succeeded, bucketed by
                                     *       the socket queue depth 0..0x20
                                     *       (addq.l #1,(0xA,A2,D1) at 0xE8765A) */
} __attribute__((packed)) route_$port_stats_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(route_$port_stats_t, deep_queue_puts) == 0x02,
               "route_$port_stats_t.deep_queue_puts");
_Static_assert(offsetof(route_$port_stats_t, failed_puts) == 0x06,
               "route_$port_stats_t.failed_puts");
_Static_assert(offsetof(route_$port_stats_t, queue_depth) == 0x0A,
               "route_$port_stats_t.queue_depth");
_Static_assert(sizeof(route_$port_stats_t) == 0x8E,
               "route_$port_stats_t must be 0x8E bytes");
#endif

/* Number of network ports supported */
#define ROUTE_$MAX_PORTS        8

/*
 * ROUTE_$PORT_ARRAY - Array of routing port structures
 *
 * Array of 8 port structures, each 0x5C (92) bytes.
 * Total size: 8 * 92 = 736 bytes (0x2E0)
 *
 * Original address: 0xE2E0A0
 */
extern route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];

/* Port type constants */
#define ROUTE_PORT_TYPE_LOCAL       1
#define ROUTE_PORT_TYPE_ROUTING     2

/*
 * ROUTE_$PORT - Current node's network port
 *
 * Contains the network port identifier for this node.
 * Set by HINT_$INIT from the hint file, or 0 if not available.
 *
 * This is the same storage as ROUTE_$PORT_ARRAY[0].network (the first
 * longword of the first port entry); see route_data.c.
 *
 * Original address: 0xE2E0A0
 */
extern uint32_t ROUTE_$PORT;

/*
 * ROUTE_$PORTP - Array of pointers to port structures
 *
 * Array of 8 pointers to route_$port_t structures, one for each
 * possible network port. Used by ROUTE_$FIND_PORT to look up
 * port info by index.
 *
 * Original address: 0xE26EE8
 */
extern route_$port_t *ROUTE_$PORTP[];

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
 * Retrieves routing statistics for a user-mode port.
 * Looks up the port by socket number (assuming network type 2),
 * then copies statistics data from the port's driver structure.
 *
 * @param socket_ptr    Pointer to socket number (uint16_t)
 * @param stats_buf     Output buffer for statistics data
 * @param length_ret    Output: number of bytes written to stats_buf
 * @param status_ret    Output: status code (status_$ok or error)
 *
 * Original address: 0x00E6A65E
 */
void ROUTE_$READ_USER_STATS(uint16_t *socket_ptr, uint8_t *stats_buf,
                            int16_t *length_ret, status_$t *status_ret);

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
 * Processes packets received from user routing ports that need
 * to be injected into the local network. Validates packet format,
 * copies data to network buffers, and queues for transmission.
 *
 * @param port_info     Port information structure (network at +6, socket at +8)
 * @param packet_data   Packet data buffer
 * @param length_ptr    Pointer to packet length
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E878A8
 */
void ROUTE_$INCOMING(void *port_info, uint8_t *packet_data,
                     uint16_t *length_ptr, status_$t *status_ret);

/*
 * ROUTE_$OUTGOING - Handle outgoing routed packets
 *
 * Retrieves queued outgoing packets from user routing ports and
 * prepares them for transmission. Finds the routing next hop,
 * copies packet data, and optionally computes a checksum.
 *
 * @param port_info     Port information (network at +6, socket at +8)
 * @param nexthop_ret   Output: next hop network address (20-bit) + flag
 * @param packet_buf    Output: packet data buffer (4-byte header + data)
 * @param length_ret    Output: total packet length including header
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E87A4E
 */
void ROUTE_$OUTGOING(void *port_info, uint32_t *nexthop_ret, uint8_t *packet_buf,
                     int16_t *length_ret, status_$t *status_ret);

/*
 * ROUTE_$SEND_USER_PORT - Send packet to user routing port
 *
 * Sends a packet through a user routing port for delivery. The packet
 * is copied to network buffers and queued to the socket.
 *
 * Parameter offsets are read off the prologue at 0x00E87C34; see
 * route/send_user_port.c for the full mapping.
 *
 * @param socket_ptr    Pointer to the port's socket number (A6+0x08)
 * @param src_addr      Source address info (A6+0x0c, never read)
 * @param hdr_va        Header source VA (A6+0x10)
 * @param hdr_len       Header length (A6+0x14)
 * @param src_pages     Source payload page array (A6+0x16)
 * @param src_data_va   Source payload VA, or 0 (A6+0x1a)
 * @param data_len      Payload byte count (A6+0x1e)
 * @param extra_ptr     Extra protocol info (A6+0x20, never read)
 * @param seq_ret       Output: packet sequence number (A6+0x24)
 * @param status_ret    Output: status code (A6+0x28)
 *
 * Original address: 0x00E87C34
 */
void ROUTE_$SEND_USER_PORT(uint16_t *socket_ptr, uint32_t src_addr, uint32_t hdr_va,
                           uint16_t hdr_len, uint32_t *src_pages,
                           uint32_t src_data_va, uint16_t data_len,
                           void *extra_ptr, uint16_t *seq_ret,
                           status_$t *status_ret);

/*
 * ROUTE_$VALIDATE_PORT - Check network capability for node
 *
 * Checks if a network operation is supported for the given routing info.
 * Iterates through ROUTE_$PORTP array looking for matching port.
 *
 * @param routing_key   Routing information
 * @param is_local      Non-zero if querying local node
 *
 * @return 0: Unknown network
 *         1: Network supports operation
 *         2: Operation not defined on hardware
 *
 * Original address: 0x00E65904
 */
int16_t ROUTE_$VALIDATE_PORT(int32_t routing_key, int8_t is_local);

/*
 * Status codes (module 0x2B = INTERNET / ROUTE)
 *
 * These are the single definitions of the status_$internet_* codes; other
 * subsystems (ring, rip, xns, ...) include this header rather than
 * redefining them.
 */
#define status_$internet_unknown_network_port   0x2B0003
#define status_$internet_illegal_port_type      0x2B0004
#define status_$route_not_routing_mode          0x2B0009
#define status_$route_invalid_ec_type           0x2B0012


/*
 * Routing port counts (route_data.c).  Shared with the RIP subsystem.
 *
 * Original addresses: 0xE26F1A, 0xE26F1C
 */
extern int16_t ROUTE_$STD_N_ROUTING_PORTS;
extern int16_t ROUTE_$N_ROUTING_PORTS;

/*
 * ROUTE_$SOCK - Routing process socket number (0xFFFF when closed)
 *
 * Used by XNS_IDP_$DEMUX to queue packets that must be forwarded.
 *
 * Original address: 0xE26F18
 */
extern uint16_t ROUTE_$SOCK;

/*
 * Routing drop counters shared with the XNS IDP demux.
 *
 * XNS_IDP_$OS_DEMUX increments these directly:
 *   0x00E18678  addq.l #0x1,(0x00E87FB4).l   no standard routing ports
 *   0x00E1869A  addq.l #0x1,(0x00E87FB0).l   IDP hop count exhausted
 *
 * The ROUTE subsystem's own definitions live in route/route_internal.h and
 * are textually identical on the m68k build; the declarations here exist so
 * that code outside ROUTE (and the host unit tests) can reach them without
 * including a foreign internal header.
 *
 * Original addresses: 0xE87FB0, 0xE87FB4
 */
#if defined(ARCH_M68K)
#define ROUTE_$STD_TOO_FAR (*(uint32_t *)0xE87FB0)
#define ROUTE_$STD_MISROUTE (*(uint32_t *)0xE87FB4)
#else
extern uint32_t ROUTE_$STD_TOO_FAR;
extern uint32_t ROUTE_$STD_MISROUTE;
#endif

/*
 * ROUTE_$DECREMENT_PORT - Decrement port counters during close
 *
 * Helper that calls RIP_$PORT_CLOSE and decrements the appropriate routing
 * port counter; may halt the router if this was the last active port.
 * RIP_$PORT (rip/misc.c) calls it directly (0x00E15818), so it is public.
 *
 * @param delete_flag      Delete notification flag
 * @param port_index       Port index being closed
 * @param port_type_flag   Port type flag (negative = STD)
 *
 * Original address: 0x00E69E40
 */
void ROUTE_$DECREMENT_PORT(int8_t delete_flag, int16_t port_index,
                           int8_t port_type_flag);

/* RIP_$HALT_PACKET, RIP_$HALT_PACKET_DATA and RIP_$SEND_DEST_ADDR carry the
 * RIP_$ prefix, so they are declared in rip/rip.h (included above) even though
 * the storage is defined in route/route_data.c (bead source-3uo). */

/*
 * Wired routing-send cells shared with RIP_$SEND's nested procedure
 * RIP_$SEND_TO_PORT_INTERNET (rip/send.c).
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

#endif /* ROUTE_H */
