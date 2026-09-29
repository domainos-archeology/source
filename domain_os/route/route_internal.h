/*
 * ROUTE - Internal Definitions
 *
 * Internal data structures, globals, and helper functions used only
 * within the ROUTE subsystem. External users should use route.h instead.
 *
 * Module data blocks (route/route.h): Claude Opus 5.5 (source-ybch).
 */

#ifndef ROUTE_INTERNAL_H
#define ROUTE_INTERNAL_H

#include "route/route.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "misc/crash_system.h"
#include "network/network.h"
#include "rip/rip.h"
#include "sock/sock.h"
#include "xns/xns.h"
#include "ring/ringlog.h"   /* RINGLOG_$LOGIT, RINGLOG_$CTL */

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
 * Global Data
 * =============================================================================
 *
 * The ROUTE module's own cells are fields of its three data blocks,
 * ROUTE_$WIRED_DATA, ROUTE_$UNWIRED_DATA and ROUTE_$RTWIRED_DATA
 * (route/route.h, defined in route/route_data.c).  ROUTE_$PORT_ARRAY /
 * ROUTE_$PORT (NET_PORT_TABLE) are in route/route.h too.
 */

/*
 * ROUTE_$SERVICE_MUTEX - Mutex for route service operations
 *
 * Lives in the RIP_WIRED segment (map 0xE26280), not in a ROUTE block: it is
 * RIP_$WIRED_DATA.route_service_mutex (rip/rip.h).
 */

/*
 * =============================================================================
 * Internal Function Prototypes
 * =============================================================================
 */

/*
 * route_$null_service_rec - the four zero bytes at 0x00E6A02C.
 *
 * Defined in route/service.c; see the comment there for the two PC-relative
 * call sites that share it (0x00E6A436 and 0x00E69FEA).
 */
extern const uint16_t route_$null_service_rec[2];

/*
 * ROUTE_$INIT_ROUTING (0x00E69CCC) is a NESTED procedure of ROUTE_$SERVICE:
 * both call sites are `bsr.w` (0x00E6A4DC, 0x00E6A512) and it reaches its
 * parent's status_ret through `movea.l (A6),A2 / (0x10,A2)` (0x00E69CD8,
 * 0x00E69D46).  It is therefore a file static, route_$init_routing, in
 * route/service.c - not a subsystem entry point (bead source-sc3x).
 */

/*
 * ROUTE_$CLOSE_PORT (0x00E69EC2) is a NESTED procedure of ROUTE_$SERVICE: it
 * is entered with `bsr.w` and no pushed arguments and reads its parent's
 * frame through `movea.l (A6),A2`.  It is therefore a file static,
 * route_$close_port, in route/service.c - not a subsystem entry point (bead
 * source-kc3d).
 */

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
 * The wired routing area (0xE87000 - 0xE88228)
 * =============================================================================
 *
 * The routing process code (RIP_RTWIRED / ROUTE_RTWIRED code) and its data
 * (RIP_RTWIRED data, ROUTE_$RTWIRED_DATA) form one contiguous region that
 * route_$wire_routing_area() wires into physical memory; its bounds are
 * constant cells in route/wire_routing_area.c.  The status cell
 * ROUTE_$SEND_USER_PORT hands to CRASH_SYSTEM is a PC-relative constant in
 * route/send_user_port.c.
 *
 * RIP_$HALT_PACKET / RIP_$HALT_PACKET_DATA are declared in rip/rip.h because
 * RIP_$HALT_ROUTER (rip/misc.c) sends the packet; RTWIRED_$SEND_FLAGS /
 * RTWIRED_$CALLBACK too (used by rip/send.c).
 *
 * The constant cells at 0x00E8789C..0x00E878A7 sit in the ROUTE_ CODE
 * segment between ROUTE_$PROCESS' `rts` (0x00E8789A) and the next routine's
 * `link.w` (0x00E878A8), carry no symbol in the SAU2 link map, and are
 * reached only PC-relative from inside ROUTE_$PROCESS.  Three of them are
 * therefore file statics in route/process.c (net_service_or_bits,
 * net_service_and_not_bits, ringlog_route_forward at 0x00E878A0 and
 * sock_empty_status).
 */

/*
 * ROUTE_$USER_STAT / route_$user_stat_t: see route/route.h.  Its allocator is
 * NET_IO_$CREATE_PORT (0x00E5A5C2), outside route/, so the record and the
 * array are public (bead source-tjv5).
 */

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

_Static_assert(offsetof(route_$internet_hdr_t, src_node)     == 0x08, "internet_hdr.src_node");
_Static_assert(offsetof(route_$internet_hdr_t, routing_type) == 0x0C, "internet_hdr.routing_type");
_Static_assert(offsetof(route_$internet_hdr_t, hdr_len)      == 0x10, "internet_hdr.hdr_len");
_Static_assert(offsetof(route_$internet_hdr_t, data_len)     == 0x14, "internet_hdr.data_len");
_Static_assert(offsetof(route_$internet_hdr_t, hdr_size)     == 0x18, "internet_hdr.hdr_size");
_Static_assert(offsetof(route_$internet_hdr_t, idp)          == 0x28, "internet_hdr.idp");

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
