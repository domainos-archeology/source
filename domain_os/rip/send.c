/*
 * RIP_$SEND - transmit a RIP packet, and RIP_$BROADCAST
 *
 * RIP_$SEND (0x00E871B6) has two nested Pascal procedures, both of which take
 * a single word parameter (the port index) and reach everything else through
 * the static link "movea.l (A6),A2":
 *
 *   RIP_$SEND_TO_PORT           0x00E870DC   XNS/IDP transmit
 *   RIP_$SEND_TO_PORT_INTERNET  0x00E87000   Domain internet transmit
 *
 * They are emitted here as statics taking an explicit pointer to the shared
 * frame (rip_$send_frame_t below), whose field names are RIP_$SEND's own A6
 * displacements.
 *
 * Note on the second one's address: 0x00E87000 also carries the label
 * RTWIRED_PROC_START, which marks the START OF THE WIRED ROUTING REGION
 * (0x00E87000..0x00E88228) that route_$wire_routing_area pins with
 * MST_$WIRE_AREA.  It is a region marker that happens to fall on this
 * procedure's entry, not the procedure's name.
 *
 * A5 in RIP_$SEND is 0x00E87D68 ("lea (0xe87d68).l,A5" at 0x00E871BE), so
 * A5+0xC is RTWIRED_$SEND_FLAGS at 0x00E87D74.
 *
 * Original addresses:
 * - RIP_$SEND_TO_PORT_INTERNET: 0x00E87000
 * - RIP_$SEND_TO_PORT:          0x00E870DC
 * - RIP_$SEND:                  0x00E871B6
 * - RIP_$BROADCAST:             0x00E87298
 */

#include "rip/rip_internal.h"
#include "route/route.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "ec/ec.h"
#include "net_io/net_io.h"
#include "network/network.h"
#include "xns/xns.h"
#include "os/os.h"

/*
 * Global data references:
 *   RIP_$STD_IDP_CHANNEL (0xE26EBC) - rip/rip.h
 *   RIP_$BCAST_CONTROL   (0xE26EC0) - rip/rip.h
 *   RIP_$INFO            (0xE263BC) - rip/rip_internal.h
 *   ROUTE_$PORT_ARRAY    (0xE2E0A0) - route/route.h
 *   RTWIRED_$CALLBACK    (0xE870D8) - route/route.h
 *   RTWIRED_$SEND_FLAGS  (0xE87D74) - route/route.h
 *   NODE_$ME             (0xE245A4) - network/network.h
 */

/* Port state values (route_$port_t.port_type at +0x2E) */
#define PORT_STATE_ACTIVE       2

/*
 * route_$port_t.active (+0x2C) is used as a BIT NUMBER, not a mask: the
 * original does "move.w (0x2c,A0),D0w / moveq #0x28,D1 / btst.l D0,D1"
 * (0x00E87240) and the same with #0x30 (0x00E87258).  btst with a data
 * register numbers bits modulo 32, so the test is
 * "(1 << (active & 0x1F)) & mask".
 */
#define PORT_ROUTE_SET_STD      0x28    /* active must be 3 or 5 */
#define PORT_ROUTE_SET_NONSTD   0x30    /* active must be 4 or 5 */

#define PORT_IN_ROUTE_SET(active, set) \
    ((((uint32_t)1 << ((active) & 0x1F)) & (uint32_t)(set)) != 0)

/*
 * The 0x1E-byte XNS IDP header RIP_$SEND_TO_PORT builds in the netbuf header
 * buffer (0x00E87112-0x00E87142).
 *
 * Note that "src" is a full 12-byte endpoint copied from the port
 * (0x00E87134-0x00E87140) and the socket half is then overwritten with 1
 * ("move.w #0x1,(0x1c,A0)" at 0x00E87142), which is why the record ends at
 * 0x1E and not 0x20.
 */
typedef struct rip_$idp_hdr_t {
    uint16_t            checksum;       /* 0x00: 0xFFFF = none  (0x00E87112) */
    uint16_t            length;         /* 0x02: 0x1E+route_len (0x00E87116) */
    uint8_t             transport_ctrl; /* 0x04: 0              (0x00E8711C) */
    uint8_t             packet_type;    /* 0x05: 1              (0x00E87120) */
    rip_$dest_addr_t    dest;           /* 0x06                 (0x00E8712E) */
    rip_$dest_addr_t    src;            /* 0x12                 (0x00E8713C) */
} __attribute__((packed)) rip_$idp_hdr_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(rip_$idp_hdr_t, dest) == 0x06, "rip_$idp_hdr_t.dest");
_Static_assert(offsetof(rip_$idp_hdr_t, src)  == 0x12, "rip_$idp_hdr_t.src");
_Static_assert(sizeof(rip_$idp_hdr_t) == 0x1E, "rip_$idp_hdr_t must be 0x1E bytes");
#endif

#define RIP_IDP_HDR_LEN     0x1E

/*
 * rip_$send_frame_t - the part of RIP_$SEND's frame both nested procedures
 * reach through the static link.  Comments give the parent A6 displacement
 * (or, for the first three, the parameter offset).
 *
 * Only some of these are live on any one path: the send record and pkt_len /
 * route_data_p are filled when flags < 0 (0x00E871CE-0x00E871F4), and pkt_id
 * when flags >= 0 (0x00E871FA).
 */
typedef struct rip_$send_frame_t {
    /* RIP_$SEND's own parameters */
    rip_$dest_addr_t   *addr_info;      /* (0x08,A6) */
    void               *route_data;     /* (0x0E,A6) */
    uint16_t            route_len;      /* (0x12,A6) */

    /* Locals shared with the nested procedures */
    int16_t             nexthop_port;   /* A6-0x72 */
    uint16_t            pkt_id;         /* A6-0x70 */
    uint16_t            hdr_len;        /* A6-0x6E */
    uint16_t            out_6c;         /* A6-0x6C */
    uint16_t            out_6a;         /* A6-0x6A */
    uint16_t            checksum;       /* A6-0x68 */
    uint16_t            pkt_len;        /* A6-0x66: RIP_IDP_HDR_LEN+route_len */
    uint32_t            hdr_va;         /* A6-0x64 */
    uint32_t            hdr_pa;         /* A6-0x60 */
    uint32_t            hdr;            /* A6-0x58 */
    net_io_$send_info_t send_info;      /* A6-0x5C: NET_IO_$SEND's ninth arg */
    status_$t           status;         /* A6-0x54 */
    void               *route_data_p;   /* A6-0x50 */
    uint32_t            hdr_data;       /* A6-0x4C */
    xns_$os_send_rec_t  send_rec;       /* A6-0x48 */
} rip_$send_frame_t;

/*
 * =============================================================================
 * RIP_$SEND_TO_PORT (nested, 0x00E870DC)
 * =============================================================================
 *
 * Transmit the packet over XNS/IDP.  The caller has already put the
 * destination address in frame->addr_info and the payload length in
 * frame->pkt_len / frame->send_rec.
 *
 * @param port_index  the word parameter at (0x8,A6)
 * @param frame       the parent frame reached through the static link
 */
static void RIP_$SEND_TO_PORT(int16_t port_index, rip_$send_frame_t *frame)
{
    route_$port_t   *port;
    rip_$idp_hdr_t  *hdr;

    /* 0x00E870EA: NETBUF_$GET_HDR(&hdr_pa, &hdr_va) */
    NETBUF_$GET_HDR(&frame->hdr_pa, &frame->hdr_va);

    /* 0x00E870FA */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /* 0x00E87108 */
    frame->hdr = frame->hdr_va;
    hdr = (rip_$idp_hdr_t *)(uintptr_t)frame->hdr;

    hdr->checksum       = 0xFFFF;           /* 0x00E87112 */
    hdr->length         = frame->pkt_len;   /* 0x00E87116 */
    hdr->transport_ctrl = 0;                /* 0x00E8711C */
    hdr->packet_type    = 1;                /* 0x00E87120 */

    /* 12 bytes of destination from the caller's record (0x00E87126-0x00E87132) */
    hdr->dest = *frame->addr_info;

    /* 12 bytes of source from port+0x20 (0x00E87134-0x00E87140) */
    hdr->src = port->xns_addr;

    hdr->src.socket = 1;                    /* 0x00E87142 */

    /* 0x00E87148: the payload goes straight after the IDP header */
    frame->hdr_data = RIP_IDP_HDR_LEN + frame->hdr;

    /* 0x00E87152-0x00E87168 */
    OS_$DATA_COPY((char *)frame->route_data_p,
                  (char *)(uintptr_t)frame->hdr_data,
                  (int32_t)frame->route_len);

    /* 0x00E8716C */
    frame->send_rec.hdr_desc.address = frame->hdr;

    /* 0x00E87172-0x00E8718A */
    XNS_IDP_$OS_SEND(&RIP_$STD_IDP_CHANNEL, &frame->send_rec,
                     &frame->checksum, &frame->status);

    /* 0x00E8718E */
    NETBUF_$RTN_HDR(&frame->hdr_va);

    /* 0x00E8719A: wake anyone waiting on an active port */
    if (port->port_type == PORT_STATE_ACTIVE) {
        EC_$ADVANCE((ec_$eventcount_t *)port->port_ec);
    }
}

/*
 * =============================================================================
 * RIP_$SEND_TO_PORT_INTERNET (nested, 0x00E87000)
 * =============================================================================
 *
 * Transmit the packet over Domain internet routing.  Ghidra also carries the
 * label RTWIRED_PROC_START at this address for the wired-region start; see
 * the file header.
 *
 * @param port_index  the word parameter at (0x8,A6)
 * @param frame       the parent frame reached through the static link
 */
static void RIP_$SEND_TO_PORT_INTERNET(int16_t port_index,
                                       rip_$send_frame_t *frame)
{
    route_$port_t *port;

    /* 0x00E8700E-0x00E87020: NETWORK_$GETHDR(&callback, &hdr_va, &hdr_pa) */
    NETWORK_$GETHDR(RTWIRED_$CALLBACK, &frame->hdr_va, &frame->hdr_pa);

    /* 0x00E87024 */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /*
     * 0x00E8702E-0x00E8707A.  The "subq.l #0x2,SP" at 0x00E8702E is the
     * Pascal function-result slot; nothing reads it.  Argument 13 is the
     * header buffer address BY VALUE ("move.l (-0x64,A2),-(SP)" at
     * 0x00E87044) - PKT_$BLD_INTERNET_HDR takes it into A2 at 0x00E12048 and
     * writes the header there.
     */
    PKT_$BLD_INTERNET_HDR(
        port->network,                  /* 1  routing_key                     */
        0,                              /* 2  dest_node                       */
        RIP_SOCKET,                     /* 3  dest_sock  (8)                  */
        (int32_t)port->network,         /* 4  src_node_or                     */
        NODE_$ME,                       /* 5  src_node                        */
        RIP_SOCKET,                     /* 6  src_sock   (8)                  */
        RIP_$BCAST_CONTROL,             /* 7  pkt_info                        */
        frame->pkt_id,                  /* 8  request_id                      */
        frame->route_data,              /* 9  template                        */
        frame->route_len,               /* 10 hdr_len                         */
        0,                              /* 11 protocol                        */
        &frame->nexthop_port,           /* 12 port_out                        */
        (uint32_t *)(uintptr_t)frame->hdr_va,  /* 13 hdr_buf, BY VALUE        */
        &frame->hdr_len,                /* 14 len_out                         */
        &frame->out_6c,                 /* 15                                 */
        &frame->out_6a,                 /* 16                                 */
        &frame->status);                /* 17 status_ret                      */

    /* 0x00E8707E */
    if (frame->status == status_$ok) {
        /* 0x00E87084-0x00E870AC */
        NET_IO_$SEND(port_index,                /* 1  port                    */
                     &frame->hdr_va,            /* 2  hdr_ptr                 */
                     frame->hdr_pa,             /* 3  hdr_pa                  */
                     frame->hdr_len,            /* 4  hdr_len                 */
                     0,                         /* 5  data_va                 */
                     RTWIRED_$CALLBACK,         /* 6  data_len (0xE870D8)     */
                     0,                         /* 7  protocol                */
                     RTWIRED_$SEND_FLAGS,       /* 8  flags   (0xE87D74)      */
                     &frame->send_info,         /* 9  send_info               */
                     &frame->status);           /* 10 status_ret              */
    }

    /* 0x00E870B0: the buffer goes back whatever happened */
    NETWORK_$RTNHDR(&frame->hdr_va);

    /* 0x00E870BC */
    if (port->port_type == PORT_STATE_ACTIVE) {
        EC_$ADVANCE((ec_$eventcount_t *)port->port_ec);
    }
}

/*
 * =============================================================================
 * RIP_$SEND
 * =============================================================================
 *
 * @param addr_info     destination address (rip_$dest_addr_t); rewritten in
 *                      place when port_index is -1
 * @param port_index    port index, or -1 for "every port"
 * @param route_data    RIP payload
 * @param route_len     RIP payload length
 * @param flags         Pascal boolean read as a byte at (0x14,A6)
 *                      ("move.b (0x14,A6),D3b / bpl" at 0x00E871C8):
 *                      < 0 = non-standard (XNS/IDP), >= 0 = standard
 *                      (Domain internet)
 *
 * Original address: 0x00E871B6
 */
void RIP_$SEND(void *addr_info, int16_t port_index, void *route_data,
               uint16_t route_len, boolean flags)
{
    rip_$send_frame_t frame;
    int16_t i;

    frame.addr_info  = (rip_$dest_addr_t *)addr_info;
    frame.route_data = route_data;
    frame.route_len  = route_len;

    if (flags < 0) {
        /*
         * 0x00E871CE-0x00E871F4: non-standard (XNS/IDP).  Prime the send
         * record; RIP_$SEND_TO_PORT fills in hdr_address per port.
         */
        frame.pkt_len      = (uint16_t)(RIP_IDP_HDR_LEN + route_len);
        frame.route_data_p = route_data;

        frame.send_rec.hdr_desc.length  = frame.pkt_len;
        frame.send_rec.hdr_desc.next    = 0;
        frame.send_rec.hdr_prebuilt = (int8_t)0xFF;      /* st */
        frame.send_rec.data_length  = 0;
        frame.send_rec.data_pages[0] = 0;
    } else {
        /* 0x00E871FA */
        frame.pkt_id = PKT_$NEXT_ID();
    }

    /* 0x00E87204 */
    if (port_index == -1) {
        /*
         * 0x00E8720A-0x00E87226: broadcast.  Three word stores put 0xFFFF at
         * the record's +4, +6 and +8, and a fourth puts 1 at +0xA.
         */
        frame.addr_info->host_hi = 0xFFFF;
        frame.addr_info->host_lo = 0xFFFFFFFFu;
        frame.addr_info->socket  = 1;

        /* 0x00E87226-0x00E87272: all 8 ports */
        for (i = 0; i < ROUTE_$MAX_PORTS; i++) {
            route_$port_t *port = &ROUTE_$PORT_ARRAY[i];

            /* 0x00E8723A */
            frame.addr_info->network = port->network;

            /* 0x00E8723C */
            if (flags < 0) {
                /* 0x00E87258 */
                if (PORT_IN_ROUTE_SET(port->active, PORT_ROUTE_SET_NONSTD)) {
                    RIP_$SEND_TO_PORT(i, &frame);
                }
            } else {
                /* 0x00E87240 */
                if (PORT_IN_ROUTE_SET(port->active, PORT_ROUTE_SET_STD)) {
                    RIP_$SEND_TO_PORT_INTERNET(i, &frame);
                }
                /*
                 * 0x00E87254 re-tests the flags byte here ("tst.b D3b / bpl
                 * 0x00E8726C").  Only the flags >= 0 path can reach it, so
                 * the branch is always taken; the original's dead test is
                 * recorded but has no effect.
                 */
            }
        }
    } else {
        /* 0x00E87278 */
        if (flags < 0) {
            RIP_$SEND_TO_PORT(port_index, &frame);
        } else {
            RIP_$SEND_TO_PORT_INTERNET(port_index, &frame);
        }
    }
}

/*
 * =============================================================================
 * RIP_$BROADCAST (0x00E87298)
 * =============================================================================
 *
 * Walk all 64 routing-table entries, build one RIP response packet holding
 * every route whose port carries the requested routing type, and hand it to
 * RIP_$SEND for every port.
 *
 * Packet layout (built at A6-0x220):
 *   +0x00  command word, always 2 (response)          (0x00E872AA)
 *   +0x02  entry 1: network longword, metric word
 *   +0x08  entry 2 ...
 * Entry n (1-based) therefore has its network at +6n-4 and its metric at +6n,
 * which is exactly what the three A6-based cursors the original keeps
 * (A6-0x244, A6-0x240, A6-0x23C, all stepped by 6 at 0x00E8731E-0x00E87326
 * and all dereferenced at -0x220) compute.
 *
 * @param flags     Pascal boolean read as a byte at (0x8,A6)
 *                  ("move.b (0x8,A6),D2b" at 0x00E872A6):
 *                  If < 0: non-standard routes (metric capped at 0x10)
 *                  If >= 0: standard routes
 */
void RIP_$BROADCAST(boolean flags)
{
    /*
     * The original's buffer is the 0x220 bytes at A6-0x220; 2 + 64*6 = 0x186
     * of them are ever written.
     */
    uint8_t         response_buf[0x220];
    int16_t         entry_count;
    int16_t         i;
    rip_$entry_t   *entry;
    rip_$route_t   *route;
    uint8_t         state;
    route_$port_t  *port;
    uint16_t        metric;
    boolean         accept;

    /* 0x00E872AA: command = 2 (response) */
    *(uint16_t *)response_buf = 2;

    entry_count = 0;

    /* 0x00E872B2: "moveq #0x3f,D0" + dbf = 64 iterations */
    for (i = 0; i < RIP_TABLE_SIZE; i++) {
        entry = &RIP_$INFO[i];

        /* 0x00E872D6: route[1] (+0x18) for non-standard, route[0] (+0x04) else */
        if (flags < 0) {
            route = &entry->routes[1];
        } else {
            route = &entry->routes[0];
        }

        /*
         * 0x00E872E4: "move.w #0xc0,D5w / and.b (0x10,A2),D5b / lsr.w #0x6,D5w
         * / beq" - any state but 0 is broadcast, including EXPIRED.
         */
        state = (uint8_t)((route->flags >> RIP_STATE_SHIFT) & 0x03);
        if (state == RIP_STATE_UNUSED) {
            continue;
        }

        /* 0x00E872F0: the port this route goes out of */
        port = &ROUTE_$PORT_ARRAY[route->port];

        /*
         * 0x00E87300-0x00E8731A.  For flags < 0 the non-standard set is
         * tested and a miss skips the entry (the "tst.b D2b / bmi" at
         * 0x00E8730E); for flags >= 0 control arrives directly at 0x00E87312
         * and only the standard set is tested.
         */
        if (flags < 0) {
            accept = PORT_IN_ROUTE_SET(port->active, PORT_ROUTE_SET_NONSTD)
                         ? (boolean)-1 : (boolean)0;
        } else {
            accept = PORT_IN_ROUTE_SET(port->active, PORT_ROUTE_SET_STD)
                         ? (boolean)-1 : (boolean)0;
        }
        if (accept >= 0) {
            continue;
        }

        /* 0x00E8731C */
        entry_count++;

        /* 0x00E8732A: network longword at +6n-4 */
        *(uint32_t *)(response_buf + 6 * entry_count - 4) = entry->network;

        /* 0x00E87332-0x00E87358: metric word at +6n */
        if (flags < 0) {
            /*
             * The non-standard arm widens to a longword before the compare
             * ("clr.l D6 / move.b (0xf,A2),D6b / addq.l #0x1,D6 /
             * cmpi.l #0x10,D6 / bls / moveq #0x10,D6").
             */
            uint32_t m = (uint32_t)route->metric + 1;
            if (m > 0x10) {
                m = 0x10;
            }
            metric = (uint16_t)m;
        } else {
            metric = (uint16_t)(route->metric + 1);
        }
        *(uint16_t *)(response_buf + 6 * entry_count) = metric;
    }

    /* 0x00E87368 */
    if (entry_count != 0) {
        /*
         * 0x00E8736C-0x00E87388.  The destination-address argument is the
         * head of the RIP module block at 0x00E87D68 ("pea (A5)"), which
         * RIP_$SEND overwrites with the broadcast host and each port's
         * network in turn.  The length is 6*count + 2 (0x00E87370-0x00E8737A).
         */
        RIP_$SEND(RIP_$SEND_DEST_ADDR, -1, response_buf,
                  (uint16_t)(6 * entry_count + 2), flags);
    }
}
