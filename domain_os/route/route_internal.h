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

/*
 * status_$internet_unknown_network_port constant cell, passed by reference to
 * CRASH_SYSTEM by ROUTE_$SEND_USER_PORT ("pea (0xD4,PC)" at 0x00E87C8E).
 */
#define ROUTE_$UNKNOWN_PORT_STATUS  (*(const status_$t *)0xE87D64)

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

extern const status_$t ROUTE_$UNKNOWN_PORT_STATUS;
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
extern uint8_t RINGLOG_$ROUTE_FORWARD[4];
extern const status_$t ROUTE_$SOCK_EMPTY_STATUS;
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
 * route_$mac_send_rec_t - the descriptor MAC_OS_$ARP fills in and MAC_OS_$SEND
 * hands to the port driver (0x4C bytes).
 *
 * The first 0x1C bytes are written by MAC_OS_$ARP (link-level addresses and
 * the frame type), the broadcast flag at +0x18 is its fourth argument, and
 * the caller fills in the rest.  MAC_OS_$SEND walks the {length, address,
 * next} descriptor at +0x1C (0x00E0B62C, 0x00E0B640-0x00E0B660), skips its
 * own buffer setup when the byte at +0x28 is true (not.b/bpl at 0x00E0B616)
 * and stores the accumulated length at +0x38 (0x00E0B786).  Everything from
 * +0x30 up is consumed by the driver entry it calls at 0x00E0B7AA.
 *
 * This is the same object as mac_os/mac_os.h's mac_os_$send_pkt_t, which
 * stops at 0x40 and so cannot describe the 16 payload page addresses at
 * +0x3C; see bead source-5lqz.  The record is built identically by
 * XNS_IDP_$SEND at 0x00E183FC - 0x00E1842A.
 */
typedef struct route_$mac_send_rec_t {
    uint8_t     link_hdr[0x18]; /* 0x00: dest/src link addresses and frame type,
                                 *       written by MAC_OS_$ARP */
    int8_t      is_broadcast;   /* 0x18: MAC_OS_$ARP's fourth argument
                                 *       (clr.b/st (A3) at 0xE0C112/0xE0C12A) */
    uint8_t     _pad_19[3];     /* 0x19 */
    uint32_t    hdr_length;     /* 0x1C: descriptor length */
    uint32_t    hdr_address;    /* 0x20: descriptor address */
    uint32_t    hdr_next;       /* 0x24: next descriptor (0 = end of chain) */
    int8_t      hdr_prebuilt;   /* 0x28: true = the header buffers are already
                                 *       set up, MAC_OS_$SEND must not build
                                 *       any of its own */
    uint8_t     _pad_29[7];     /* 0x29 */
    uint32_t    frame_type;     /* 0x30: 0x600 for both ROUTE and XNS IDP */
    uint32_t    _pad_34;        /* 0x34 */
    uint32_t    data_length;    /* 0x38: payload byte count */
    uint32_t    data_pages[4];  /* 0x3C: payload page addresses */
} route_$mac_send_rec_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(route_$mac_send_rec_t, is_broadcast) == 0x18, "mac_send.is_broadcast");
_Static_assert(offsetof(route_$mac_send_rec_t, hdr_length)   == 0x1C, "mac_send.hdr_length");
_Static_assert(offsetof(route_$mac_send_rec_t, hdr_address)  == 0x20, "mac_send.hdr_address");
_Static_assert(offsetof(route_$mac_send_rec_t, hdr_next)     == 0x24, "mac_send.hdr_next");
_Static_assert(offsetof(route_$mac_send_rec_t, hdr_prebuilt) == 0x28, "mac_send.hdr_prebuilt");
_Static_assert(offsetof(route_$mac_send_rec_t, frame_type)   == 0x30, "mac_send.frame_type");
_Static_assert(offsetof(route_$mac_send_rec_t, data_length)  == 0x38, "mac_send.data_length");
_Static_assert(offsetof(route_$mac_send_rec_t, data_pages)   == 0x3C, "mac_send.data_pages");
_Static_assert(sizeof(route_$mac_send_rec_t) == 0x4C, "route_$mac_send_rec_t must be 0x4C bytes");
#endif

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
