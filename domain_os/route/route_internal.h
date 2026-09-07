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
#include "xns/xns.h"

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
 * ROUTE_$ROUTING - "the router is running" flag
 *
 * A one-BYTE Domain boolean, not a word: ROUTE_$PROCESS sets it with
 * "st (0x00E26F1E).l" (0x00E8742E) and clears it with "clr.b (0x00E26F1E).l"
 * (0x00E87812), and the SMD readers test it with "tst.b" (0x00E69B8C,
 * 0x00E69E94).  SMD_$DISP1_INT is the *code* at 0x00E26F20, immediately
 * after this byte - the two are distinct symbols (bead source-8xb).
 *
 * Original address: 0xE26F1E
 */
extern boolean ROUTE_$ROUTING;

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

/* ROUTE_$DECREMENT_PORT (0x00E69E40) is declared in route/route.h
 * (RIP_$PORT_CLOSE calls it). */

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
 * The routine that used to be declared here as RTWIRED_PROC_START is a nested
 * Pascal procedure of RIP_$SEND; it is a static in rip/send.c called
 * RIP_$SEND_TO_PORT_INTERNET.  RTWIRED_PROC_START is the name of the address
 * itself (0x00E87000), the start of the wired routing region below.
 */

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

/*
 * status_$internet_unknown_network_port constant cell, passed by reference to
 * CRASH_SYSTEM by ROUTE_$SEND_USER_PORT ("pea (0xD4,PC)" at 0x00E87C8E).
 */
#define ROUTE_$UNKNOWN_PORT_STATUS  (*(const status_$t *)0xE87D64)

/* RIP_$HALT_PACKET / RIP_$HALT_PACKET_DATA are declared in route/route.h
 * because RIP_$HALT_ROUTER (rip/misc.c) sends the packet. */

/* RTWIRED_$SEND_FLAGS / RTWIRED_$CALLBACK: see route/route.h (used by rip/send.c) */

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

/*
 * Constant cells in the routing code segment, all passed by reference
 * (pea (d,PC)) by ROUTE_$PROCESS:
 *   0xE8789C  NETWORK_$SET_SERVICE opcode 0 "or bits"   (pea (0x45E,PC) @0xE8743C)
 *   0xE8789E  NETWORK_$SET_SERVICE opcode 1 "and not"   (pea (0x76,PC)  @0xE87826)
 *   0xE878A0  RINGLOG_$LOGIT header info; only byte 0 is read, and only its
 *             bit 7 (the "inbound" flag), at 0x00E1A2F6  (pea (0x292,PC) @0xE8760C)
 *   0xE878A4  status_$network_buffer_queue_is_empty, handed to CRASH_SYSTEM
 *             when SOCK_$GET returns false             (pea (0x3E6,PC) @0xE874BC)
 */
#define ROUTE_$NET_SERVICE_ON   (*(int16_t *)0xE8789C)
#define ROUTE_$NET_SERVICE_OFF  (*(int16_t *)0xE8789E)
#define RINGLOG_$ROUTE_FORWARD  ((uint8_t *)0xE878A0)
#define ROUTE_$SOCK_EMPTY_STATUS (*(const status_$t *)0xE878A4)

/* Send callback/data pointer (4 bytes of zeros at 0xE870D8) */

/* Routing process state */
#define ROUTE_$PROCESS_UID      (*(uint16_t *)0xE88216)
#define ROUTE_$CHECKSUM_ENABLED (*(int8_t *)0xE88218)
#define ROUTE_$SERVICE_ID       (*(uint32_t *)0xE8821C)
#define PTR_ROUTE_$CONTROL_EC   (*(ec_$eventcount_t **)0xE88220)
#define ROUTE_$FWD_TIMEOUT      (*(uint16_t *)0xE88224)
#define ROUTE_$PACKET_SEQ       (*(uint16_t *)0xE88226)

/* Time of the last routing update (A5 base of the wired data) */
#define ROUTE_$LAST_UPDATE_TIME (*(uint32_t *)0xE825DC)

/*
 * ROUTE_$ANNOUNCE_TEMPLATE - the two-byte RIP template ROUTE_$ANNOUNCE_NET
 * sends (the constant word 2, i.e. a RIP response with no entries).
 * ROUTE_$ANNOUNCE_NET reaches it as "pea (0x4,A5)" at 0x00E69FF2 with A5 left
 * at 0x00E825DC by ROUTE_$SERVICE (0x00E6A038) - the function never loads A5
 * of its own.
 */
#define ROUTE_$ANNOUNCE_TEMPLATE (*(const uint16_t *)0xE825E0)
#else
extern char ROUTE_$WIRED_AREA_START_SYM[];
extern char ROUTE_$WIRED_AREA_END_SYM[];
#define ROUTE_$WIRED_AREA_START ((void *)ROUTE_$WIRED_AREA_START_SYM)
#define ROUTE_$WIRED_AREA_END   ((void *)ROUTE_$WIRED_AREA_END_SYM)

extern const status_$t ROUTE_$UNKNOWN_PORT_STATUS;
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
extern uint8_t RINGLOG_$ROUTE_FORWARD[4];
extern const status_$t ROUTE_$SOCK_EMPTY_STATUS;
extern uint16_t ROUTE_$PROCESS_UID;
extern int8_t ROUTE_$CHECKSUM_ENABLED;
extern uint32_t ROUTE_$SERVICE_ID;
extern ec_$eventcount_t *PTR_ROUTE_$CONTROL_EC;
extern uint16_t ROUTE_$FWD_TIMEOUT;
extern uint16_t ROUTE_$PACKET_SEQ;
extern uint32_t ROUTE_$LAST_UPDATE_TIME;
extern const uint16_t ROUTE_$ANNOUNCE_TEMPLATE;
#endif

/*
 * =============================================================================
 * Packet record layouts used by the forwarding path (ROUTE_$PROCESS)
 * =============================================================================
 */

/*
 * route_$internet_hdr_t - the Domain internet header that PKT_$BLD_INTERNET_HDR
 * builds and that SOCK_$GET hands back in sock_$pkt_info_t.hdr.
 *
 * Field offsets were taken from the stores in PKT_$BLD_INTERNET_HDR
 * (0x00E1206A - 0x00E122BA, A2 = header buffer); only the fields
 * ROUTE_$PROCESS touches are named.  For "standard" (pure XNS) routing the
 * buffer holds the IDP header at offset 0 instead and none of these fields
 * apply; ROUTE_$PROCESS picks the IDP base with the branch at 0x00E87500.
 */
typedef struct route_$internet_hdr_t {
    uint32_t    dest_node;      /* 0x00: 20-bit destination node id
                                 *       (move.l D1,(A2) at 0xE1217C); rewritten
                                 *       with the next hop at 0xE875F8 */
    uint32_t    route_info;     /* 0x04: masked to 0xFF0000FF at 0xE12070 */
    uint32_t    src_node;       /* 0x08: NODE_$ME (0xE1207C); rewritten at
                                 *       0xE875E6 when this node forwards */
    uint8_t     routing_type;   /* 0x0C: low byte of the caller's routing type
                                 *       word (1 = local, 2 = internet;
                                 *       move.b (0x3,A3),(0xC,A2) at 0xE1228A) */
    uint8_t     _pad_0d;        /* 0x0D: cleared at 0xE12290 */
    uint8_t     _f0e;           /* 0x0E: move.b (0x1,A3),(0xE,A2) at 0xE12298 */
    uint8_t     _pad_0f;        /* 0x0F: cleared at 0xE12294 */
    uint16_t    hdr_len;        /* 0x10: total header length (0xE122BA) */
    uint16_t    template_len;   /* 0x12: caller template length (0xE1229E) */
    uint16_t    data_len;       /* 0x14: payload byte count (0xE122A2) */
    uint16_t    _f16;           /* 0x16: 0xE122A6 */
    uint8_t     hdr_size;       /* 0x18: header size byte (0x28 or 4) */
    uint8_t     pkt_type;       /* 0x19: 4 = internet, 1 = local */
    uint16_t    _f1a;           /* 0x1A */
    uint16_t    _f1c;           /* 0x1C */
    uint16_t    _f1e;           /* 0x1E */
    uint8_t     _f20[0x08];     /* 0x20 */
    xns_$idp_header_t idp;      /* 0x28: the encapsulated IDP header
                                 *       (lea (0x28,A4),A2 at 0xE87508) */
} route_$internet_hdr_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(route_$internet_hdr_t, src_node)     == 0x08, "internet_hdr.src_node");
_Static_assert(offsetof(route_$internet_hdr_t, routing_type) == 0x0C, "internet_hdr.routing_type");
_Static_assert(offsetof(route_$internet_hdr_t, hdr_len)      == 0x10, "internet_hdr.hdr_len");
_Static_assert(offsetof(route_$internet_hdr_t, data_len)     == 0x14, "internet_hdr.data_len");
_Static_assert(offsetof(route_$internet_hdr_t, hdr_size)     == 0x18, "internet_hdr.hdr_size");
_Static_assert(offsetof(route_$internet_hdr_t, idp)          == 0x28, "internet_hdr.idp");
#endif

/*
 * The 0x4C-byte descriptor ROUTE_$PROCESS builds for MAC_OS_$SEND is
 * mac_os_$send_pkt_t (mac_os/mac_os.h); its layout and the evidence for it
 * are documented there.  ROUTE_$PROCESS's copy lives at A6-0x50.
 */

/*
 * ROUTE_$PORT_STATS - the statistics block a port's driver_stats field names.
 *
 * On the target this is simply the 32-bit address stored in the port entry
 * ("movea.l (0x44,A3),A2" at 0x00E87618).  It is spelled as a macro so that a
 * host unit test, where a real pointer does not fit in the 32-bit field, can
 * rebind the dereference without changing the record layout.
 */
#define ROUTE_$PORT_STATS(port) \
    ((route_$port_stats_t *)(uintptr_t)(port)->driver_stats)

/* Frame type ROUTE_$PROCESS and XNS_IDP_$SEND put in route_$mac_send_rec_t */
#define ROUTE_$MAC_FRAME_TYPE   0x600

/* Lock ids ROUTE_$PROCESS takes: PROC1_$SET_LOCK(0xD), ML_$LOCK(0x18) */
#define ROUTE_$PROC_LOCK_ID     0x0D
#define ROUTE_$NET_IO_LOCK_ID   0x18

/* Ticks between periodic RIP broadcasts (moveq #0x72 at 0x00E877EE) */
#define ROUTE_$BROADCAST_INTERVAL   0x72

/* Largest packet ROUTE_$PROCESS will forward onto a real port (0x00E8766C) */
#define ROUTE_$MAX_FORWARD_SIZE     0x400

/* IDP transport-control value at which a packet is discarded (0x00E8754A) */
#define ROUTE_$MAX_HOP_COUNT        0x10

#endif /* ROUTE_INTERNAL_H */
