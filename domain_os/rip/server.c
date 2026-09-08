/*
 * RIP_$SERVER and friends - the RIP protocol server (0x00E68864-0x00E68E24)
 *
 * Functions in this translation unit, in image order:
 *
 *   RIP_$PACKET_LENGTH    0x00E68864   entry_count * 6 + 2
 *   RIP_$SEND_UPDATES     0x00E6887A   broadcast if the change flag is set
 *   RIP_$PROCESS_REQUEST  0x00E688C8   nested procedure: builds the reply
 *   RIP_$SERVER           0x00E68A08   one packet off socket 8
 *
 * RIP_$PROCESS_REQUEST is a nested Pascal procedure: it takes one boolean
 * parameter and reaches everything else through the static link
 * ("move.l (A6),D6" at 0x00E688D4), so it is a static here that takes a
 * pointer to the parent's frame (rip_$server_frame_t).
 *
 * The whole of RIP_$SERVER (0x00E68A08-0x00E68E24) was re-emitted block for
 * block against the disassembly for bead source-4nvz; every A6 displacement
 * the original uses is a named field of rip_$server_frame_t below.
 *
 * The frame's two 32-bit virtual addresses (payload_va and hdr_va) go through
 * ARCH_PTR_TO_VA / ARCH_VA_TO_PTR: on m68k both are the identity cast, and a
 * host test can point ARCH_HOST_VA_BASE at its own arena so a 64-bit pointer
 * still survives the round trip through a uint32_t field.
 */

#include "rip/rip_internal.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"
#include "time/time.h"
#include "rem_name/rem_name.h"  /* REM_NAME_$REGISTER_SERVER */
#include "hint/hint.h"
#include "uid/uid.h"        /* NODE_$ME (0xE245A4) */
#include "xns/xns.h"        /* xns_$idp_header_t: the header the XNS path copies */

/*
 * =============================================================================
 * Wire formats
 * =============================================================================
 *
 * A RIP packet is a 2-byte command followed by 6-byte entries:
 *
 *   +0x00  command   1 = request, 2 = response, 3 = name-service register
 *   +0x02  entry 0   { network:4, metric:2 }
 *   +0x08  entry 1
 *   ...
 *
 * RIP_$PACKET_LENGTH(n) is exactly 6n + 2 and RIP_$SERVER refuses anything
 * whose payload length disagrees with it (0x00E68B04-0x00E68B14).  The
 * request arm reads entry i at request + 2 + 6i ("lea (0,A1,D5),A2" with
 * D5 = 6(i+1) then "(-0x4,A2)" at 0x00E68904) and the response arm at
 * packet_data + 2 + 6i ("(-0x4d4,A2)" with A2 = A6 + 6 + 6i at 0x00E68DAC).
 */
typedef struct rip_$pkt_entry_t {
    uint32_t    network;                /* 0x00 */
    uint16_t    metric;                 /* 0x04 */
} __attribute__((packed)) rip_$pkt_entry_t;

typedef struct rip_$packet_t {
    uint16_t            command;                    /* 0x000 */
    rip_$pkt_entry_t    entries[RIP_MAX_ENTRIES];   /* 0x002 */
} __attribute__((packed)) rip_$packet_t;

/* 2 + 90*6 = 542 = 0x21E, the size RIP_$SERVER hands PKT_$BRK_INTERNET_HDR
 * as its data_max ("move.w #0x21e,-(SP)" at 0x00E68A96) and copies on the
 * XNS path (0x87 longwords plus a word, 0x00E68A78-0x00E68A82). */
_Static_assert(sizeof(rip_$pkt_entry_t) == RIP_ENTRY_SIZE, "rip_$pkt_entry_t");
_Static_assert(sizeof(rip_$packet_t) == 0x21E, "rip_$packet_t must be 0x21E bytes");

/*
 * =============================================================================
 * RIP_$SERVER's stack frame
 * =============================================================================
 *
 * "link.w A6,-0x538" at 0x00E68A08.  Offsets below are from the frame base,
 * i.e. field offset = 0x538 + <A6 displacement>.  RIP_$PROCESS_REQUEST reads
 * and writes five of these fields through the static link, which is why the
 * frame is a named record rather than a pile of C locals.
 *
 *   A6-0x538  reg_node_id      A6-0x4F4  src_node_or
 *   A6-0x51C  dest_sock        A6-0x4F0  payload_va
 *   A6-0x51A  src_sock         A6-0x4EC  status
 *   A6-0x518  id_out           A6-0x4E0  reg_network
 *   A6-0x516  data_len         A6-0x4DC  wait_delay (clock_t, 6 bytes)
 *   A6-0x514  entry_count      A6-0x4D4  wait_status
 *   A6-0x512  response_count   A6-0x4D0  packet_data (0x21E bytes)
 *   A6-0x50E  send_retry_hint  A6-0x2B0  info_out (PKT_$BRK's 30-byte record)
 *   A6-0x50C  send_timeout     A6-0x290  response (0x220 bytes)
 *   A6-0x508  hdr_va           A6-0x070  pkt (sock_$pkt_info_t, 0x40 bytes)
 *   A6-0x500  dest_node        A6-0x030  source_addr (rip_$xns_addr_t)
 *   A6-0x4FC  network          A6-0x020  header (xns_$idp_header_t, 30 bytes)
 *   A6-0x4F8  src_node         A6-0x002  header_tail
 */
typedef struct rip_$server_frame_t {
    /* 0x000 */ uint32_t            reg_node_id;
    /* 0x004 */ uint8_t             _gap_004[0x18];
    /* 0x01C */ uint16_t            dest_sock;      /* BRK arg 5  */
    /* 0x01E */ uint16_t            src_sock;       /* BRK arg 8  */
    /* 0x020 */ uint16_t            id_out;         /* BRK arg 10 */
    /* 0x022 */ uint16_t            data_len;       /* BRK arg 13 */
    /* 0x024 */ int16_t             entry_count;
    /* 0x026 */ int16_t             response_count;
    /* 0x028 */ uint16_t            _gap_028;
    /* 0x02A */ uint16_t            send_retry_hint;
    /* 0x02C */ uint16_t            send_timeout;
    /* 0x02E */ uint16_t            _gap_02e;
    /* 0x030 */ uint32_t            hdr_va;
    /* 0x034 */ uint32_t            _gap_034;
    /* 0x038 */ uint32_t            dest_node;      /* BRK arg 4 */
    /* 0x03C */ uint32_t            network;        /* BRK arg 3 */
    /* 0x040 */ uint32_t            src_node;       /* BRK arg 7 */
    /* 0x044 */ uint32_t            src_node_or;    /* BRK arg 6 */
    /* 0x048 */ uint32_t            payload_va;
    /* 0x04C */ status_$t           status;         /* BRK arg 14 */
    /* 0x050 */ uint32_t            _gap_050[2];
    /* 0x058 */ uint32_t            reg_network;
    /* 0x05C */ clock_t             wait_delay;
    /* 0x062 */ uint16_t            _gap_062;
    /* 0x064 */ status_$t           wait_status;
    /* 0x068 */ rip_$packet_t       packet_data;    /* BRK arg 11 */
    /* 0x286 */ uint16_t            _gap_286;
    /* 0x288 */ uint16_t            info_out[15];   /* BRK arg 9, 30 bytes */
    /* 0x2A6 */ uint16_t            _gap_2a6;
    /* 0x2A8 */ rip_$packet_t       response;
    /* 0x4C6 */ uint16_t            _gap_4c6;
    /* 0x4C8 */ sock_$pkt_info_t    pkt;
    /* 0x508 */ rip_$xns_addr_t     source_addr;
    /* 0x512 */ uint8_t             _gap_512[6];
    /* 0x518 */ xns_$idp_header_t   header;
    /* 0x536 */ uint8_t             header_tail[2];
} rip_$server_frame_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(rip_$server_frame_t, dest_sock)       == 0x01C, "frame.dest_sock");
_Static_assert(offsetof(rip_$server_frame_t, src_sock)        == 0x01E, "frame.src_sock");
_Static_assert(offsetof(rip_$server_frame_t, id_out)          == 0x020, "frame.id_out");
_Static_assert(offsetof(rip_$server_frame_t, data_len)        == 0x022, "frame.data_len");
_Static_assert(offsetof(rip_$server_frame_t, entry_count)     == 0x024, "frame.entry_count");
_Static_assert(offsetof(rip_$server_frame_t, response_count)  == 0x026, "frame.response_count");
_Static_assert(offsetof(rip_$server_frame_t, send_retry_hint) == 0x02A, "frame.send_retry_hint");
_Static_assert(offsetof(rip_$server_frame_t, send_timeout)    == 0x02C, "frame.send_timeout");
_Static_assert(offsetof(rip_$server_frame_t, hdr_va)          == 0x030, "frame.hdr_va");
_Static_assert(offsetof(rip_$server_frame_t, dest_node)       == 0x038, "frame.dest_node");
_Static_assert(offsetof(rip_$server_frame_t, network)         == 0x03C, "frame.network");
_Static_assert(offsetof(rip_$server_frame_t, src_node)        == 0x040, "frame.src_node");
_Static_assert(offsetof(rip_$server_frame_t, src_node_or)     == 0x044, "frame.src_node_or");
_Static_assert(offsetof(rip_$server_frame_t, payload_va)      == 0x048, "frame.payload_va");
_Static_assert(offsetof(rip_$server_frame_t, status)          == 0x04C, "frame.status");
_Static_assert(offsetof(rip_$server_frame_t, reg_network)     == 0x058, "frame.reg_network");
_Static_assert(offsetof(rip_$server_frame_t, wait_delay)      == 0x05C, "frame.wait_delay");
_Static_assert(offsetof(rip_$server_frame_t, wait_status)     == 0x064, "frame.wait_status");
_Static_assert(offsetof(rip_$server_frame_t, packet_data)     == 0x068, "frame.packet_data");
_Static_assert(offsetof(rip_$server_frame_t, info_out)        == 0x288, "frame.info_out");
_Static_assert(offsetof(rip_$server_frame_t, response)        == 0x2A8, "frame.response");
_Static_assert(offsetof(rip_$server_frame_t, pkt)             == 0x4C8, "frame.pkt");
_Static_assert(offsetof(rip_$server_frame_t, source_addr)     == 0x508, "frame.source_addr");
_Static_assert(offsetof(rip_$server_frame_t, header)          == 0x518, "frame.header");
_Static_assert(offsetof(rip_$server_frame_t, header_tail)     == 0x536, "frame.header_tail");
_Static_assert(sizeof(rip_$server_frame_t) == 0x538, "rip_$server_frame_t must be 0x538 bytes");
#endif

/*
 * =============================================================================
 * Constants the original passes by reference
 * =============================================================================
 */

/*
 * "pea (0x232,PC)" at 0x00E68BF2 resolves to 0x00E68E26, a zero word in the
 * code region: TIME_$WAIT's delay type (0 = relative).  A named file-static
 * cell, per the 2026-09-06 audit's rule for `pea (d,PC)` constants.
 */
static uint16_t rip_$server_wait_delay_type = 0;    /* 0x00E68E26 */

/*
 * "pea (0x1de,PC)" at 0x00E68C48 resolves to 0x00E68E28, the same zero cell
 * RIP_$ANNOUNCE_NS passes as PKT_$SEND_INTERNET's data pointer with a length
 * of zero (rip/misc.c, "pea (-0x35a,PC)" at 0x00E69192).  Declared once, in
 * rip/rip_internal.h, as RIP_$ANNOUNCE_EXTRA.
 */

/* 0x000D0003 "quit while waiting for event" - the retry loop's exit test at
 * 0x00E68C00. */

/* Response retry loop (0x00E68BB8-0x00E68C10): five attempts, 25000 ticks
 * apart. */
#define RIP_SEND_RETRIES        5
#define RIP_SEND_DELAY_LOW      0x61A8      /* 25000, clock_t.low */

/*
 * =============================================================================
 * RIP_$PACKET_LENGTH (0x00E68864)
 * =============================================================================
 *
 * "add.w D0w,D0w / move.w D0w,D1w / add.w D1w,D1w / add.w D1w,D0w /
 *  addq.w #2,D0w" - entry_count * 6 + 2, computed entirely in 16 bits.
 *
 * @param entry_count   Number of route entries (word at (0x8,A6))
 * @return              Packet data length in bytes
 */
int16_t RIP_$PACKET_LENGTH(int16_t entry_count)
{
    int16_t d0 = (int16_t)(entry_count + entry_count);   /* 0x00E6886C */
    int16_t d1 = (int16_t)(d0 + d0);                     /* 0x00E68870 */

    return (int16_t)(d1 + d0 + 2);                       /* 0x00E68872 */
}

/*
 * =============================================================================
 * RIP_$SEND_UPDATES (0x00E6887A)
 * =============================================================================
 *
 * Broadcasts the routing table if the matching "recent changes" flag is set
 * and there is more than one routing port.  The flag is cleared first, so a
 * change that arrives during the broadcast is not lost.
 *
 * @param is_std    Pascal boolean at (0x8,A6), read with
 *                  "move.b (0x8,A6),D0b / bpl" at 0x00E6887E:
 *                    < 0  -> the STD (XNS) side: ROUTE_$STD_N_ROUTING_PORTS,
 *                            RIP_$STD_RECENT_CHANGES, RIP_$BROADCAST(true)
 *                    >= 0 -> the Domain-internet side: ROUTE_$N_ROUTING_PORTS,
 *                            RIP_$RECENT_CHANGES, RIP_$BROADCAST(false)
 */
void RIP_$SEND_UPDATES(boolean is_std)
{
    boolean flags;

    if (is_std < 0) {
        /* 0x00E68884-0x00E6889E */
        if (ROUTE_$STD_N_ROUTING_PORTS <= 1) {
            return;
        }
        if (RIP_$STD_RECENT_CHANGES >= 0) {
            return;
        }
        RIP_$STD_RECENT_CHANGES = 0;
        flags = true;
    } else {
        /* 0x00E688A2-0x00E688BC */
        if (ROUTE_$N_ROUTING_PORTS <= 1) {
            return;
        }
        if (RIP_$RECENT_CHANGES >= 0) {
            return;
        }
        RIP_$RECENT_CHANGES = 0;
        flags = false;
    }

    RIP_$BROADCAST(flags);                              /* 0x00E688BE */
}

/*
 * =============================================================================
 * RIP_$PROCESS_REQUEST (0x00E688C8) - nested procedure
 * =============================================================================
 *
 * Builds the reply to an incoming RIP request in the parent's response
 * buffer.  Nested: "move.l (A6),D6" at 0x00E688D4 takes RIP_$SERVER's frame
 * pointer out of the static link and every other operand is reached through
 * it.  The five parent fields it touches are entry_count (read),
 * response_count (read and written), payload_va (read) and the response
 * buffer (written).
 *
 * Two modes, exactly as in the original:
 *   - each requested network is looked up individually; the first entry whose
 *     network is 0xFFFFFFFF abandons that loop (0x00E6890A) and
 *   - the whole table is enumerated instead (0x00E6896E), capped at
 *     RIP_MAX_ENTRIES answers.
 *
 * @param is_std    (0x8,A6), "move.b (0x8,A6),D2b" at 0x00E688D0.
 *                  < 0 selects routes[1] (the STD/XNS slot) and clamps the
 *                  metric up to 0x10; >= 0 selects routes[0] and reports
 *                  0x11 for an unknown network.
 * @param f         The parent frame (the static link).
 */
static void RIP_$PROCESS_REQUEST(boolean is_std, rip_$server_frame_t *f)
{
    boolean         full_table;     /* D3 */
    int16_t         i;              /* the dbf index, 0-based */
    int16_t         j;
    uint32_t        network;
    rip_$entry_t   *entry;          /* A0 - RIP_$NET_LOOKUP's result */
    rip_$route_t   *route;          /* A1 */
    uint32_t        metric_l;       /* D0/D1 as a longword on the STD side */
    uint16_t        metric_w;       /* D0/D1 as a word on the internet side */
    uint16_t        state;

    /*
     * The request the parent received.  On the XNS path this is
     * packet + 0x1E in the netbuf, set at 0x00E68A5A.
     *
     * ORIGINAL DEFECT, PRESERVED (bead source-u9wy, confirmed instruction by
     * instruction).  RIP_$SERVER writes (-0x4f0,A6) at exactly one place,
     * 0x00E68A5A `move.l D4,(-0x4f0,A6)`, and that store sits inside the XNS
     * arm gated by 0x00E68A4E `tst.b D3b` / 0x00E68A50 `bpl.b 0x00E68A8E`.
     * The Domain-internet path reaches this procedure through 0x00E68C2A
     * `clr.w -(SP)` / 0x00E68C2C `bsr.w 0x00E688C8` without ever taking that
     * arm, yet 0x00E688F6 `movea.l (-0x4f0,A0),A1` loads the slot on both
     * paths - so an internet-borne request with entry_count > 0 reads the
     * requested networks through whatever the previous stack frame left there.
     *
     * The intended pointer was almost certainly &(-0x4d0,A6):
     * PKT_$BRK_INTERNET_HDR puts the internet payload there, and that is
     * where the response arm reads it back from (0x00E68DA4
     * `move.w (-0x4d0,A2),-(SP)`).  On the XNS path the two agree, because
     * 0x00E68A70-0x00E68A82 copies 0x21E bytes from (-0x4f0,A6) to
     * (-0x4d0,A6).  No fix is possible without changing behaviour.
     */
    const rip_$packet_t *request = (const rip_$packet_t *)ARCH_VA_TO_PTR(f->payload_va);

    f->response.command = RIP_CMD_RESPONSE;             /* 0x00E688D8 */
    full_table = false;                                 /* 0x00E688DE */
    f->response_count = f->entry_count;                 /* 0x00E688E0 */

    /* "move.w (-0x514,A0),D0w / subq.w #1,D0w / bmi" - nothing to answer */
    if ((int16_t)(f->entry_count - 1) < 0) {            /* 0x00E688E6 */
        goto check_full_table;
    }

    /* 0x00E688F4-0x00E68964: dbf over entry_count requested networks */
    for (i = 0; i < f->entry_count; i++) {
        network = request->entries[i].network;           /* 0x00E68904 */

        if (network == 0xFFFFFFFF) {
            full_table = true;                           /* 0x00E6890A */
            goto full_table_scan;                        /* 0x00E6890C */
        }

        /* 0x00E6890E: one "clr.l -(SP)" covers both boolean word slots */
        entry = RIP_$NET_LOOKUP(network, 0, 0);

        f->response.entries[i].network = network;        /* 0x00E6891C */

        if (is_std < 0) {                                /* 0x00E68922 */
            if (entry == NULL) {                         /* 0x00E68926 */
                f->response.entries[i].metric = 0x10;    /* 0x00E6892C */
                continue;
            }
            /* Longword arithmetic and an UNSIGNED compare (0x00E6893C) */
            metric_l = (uint32_t)entry->routes[1].metric + 1;
            if (metric_l <= 0x10) {
                metric_l = 0x10;                         /* 0x00E68944 */
            }
            f->response.entries[i].metric = (uint16_t)metric_l;  /* 0x00E6895E */
        } else {
            if (entry == NULL) {                         /* 0x00E68948 */
                f->response.entries[i].metric = 0x11;    /* 0x00E6894E */
                continue;
            }
            metric_w = (uint16_t)(entry->routes[0].metric + 1);  /* 0x00E68956 */
            f->response.entries[i].metric = metric_w;    /* 0x00E6895E */
        }
    }

check_full_table:
    /*
     * 0x00E68968: D3 can only be false here - the one place that sets it
     * branches straight to the enumeration - but the original tests it, so
     * the test stays.
     */
    if (full_table >= 0) {
        return;                                          /* 0x00E689FE */
    }

full_table_scan:
    /* 0x00E6896E-0x00E689FA: 64 table slots, "moveq #0x3f,D0" + dbf */
    f->response_count = 0;                               /* 0x00E68970 */

    for (j = 0; j <= RIP_TABLE_SIZE - 1; j++) {
        entry = &RIP_$INFO[j];                           /* stride 0x2C */

        if (is_std < 0) {
            route = &entry->routes[1];                   /* 0x00E68986: +0x18 */
        } else {
            route = &entry->routes[0];                   /* 0x00E6898C: +0x04 */
        }

        /* "move.w #0xc0,D1w / and.b (0x10,A1),D1b / lsr.w #6,D1w" */
        state = (uint16_t)((route->flags & RIP_STATE_MASK) >> RIP_STATE_SHIFT);

        /* "moveq #6,D4 / btst.l D1,D4" - VALID (1) or AGING (2) */
        if (((1u << state) & 6u) == 0) {                 /* 0x00E6899C */
            continue;
        }

        f->response_count++;                             /* 0x00E689A2 */

        /* A4 = frame + 6*response_count; the store is at (-0x294,A4), i.e.
         * response + 2 + 6*(response_count - 1). */
        f->response.entries[f->response_count - 1].network = entry->network;

        if (is_std < 0) {                                /* 0x00E689BA */
            metric_l = (uint32_t)route->metric + 1;
            if (metric_l <= 0x10) {
                metric_l = 0x10;                         /* 0x00E689CE */
            }
            f->response.entries[f->response_count - 1].metric = (uint16_t)metric_l;
        } else {
            metric_w = (uint16_t)(route->metric + 1);    /* 0x00E689D4 */
            f->response.entries[f->response_count - 1].metric = metric_w;
        }

        if (f->response_count == RIP_MAX_ENTRIES) {      /* 0x00E689EE */
            return;
        }
    }
}

/*
 * =============================================================================
 * RIP_$SERVER (0x00E68A08)
 * =============================================================================
 *
 * Takes one packet off socket 8 and dispatches on its command word.  The
 * caller (NETWORK_$SOCKET_SERVER, 0x00E11BA8) loops.
 *
 * Register aliases used below, as the original assigns them:
 *   A2  packet     the header buffer VA out of sock_$pkt_info_t.hdr
 *   A3  page_base  A2 rounded down to a 1KB netbuf page
 *   D2  first the payload length, then the port index
 *   D3  is_std     bit 1 of sock_$pkt_info_t.flags, then the retry counter
 *   D4  the network the packet came in on
 */
void RIP_$SERVER(void)
{
    rip_$server_frame_t f;

    xns_$idp_header_t  *packet;         /* A2 */
    const uint8_t      *page_base;      /* A3 */
    boolean             got_packet;     /* D0b */
    boolean             is_std;         /* D3b */
    int16_t             port_index;     /* D2w, after 0x00E68B44 */
    uint16_t            retries;        /* D3w, after 0x00E68BB6 */
    uint32_t            network;        /* D4 */
    uint16_t            port_network;
    route_$port_t      *port;           /* A2, in the response arm */
    uint16_t            i;
    int16_t             n;

    /* 0x00E68A10-0x00E68A24: a Pascal function; nothing to do if the queue
     * was empty. */
    got_packet = (boolean)SOCK_$GET(RIP_SOCKET, &f.pkt);
    if (got_packet >= 0) {
        return;                                          /* 0x00E68E1C */
    }

    /*
     * 0x00E68A28: "btst.b #0x1,(-0x5f,A6) / sne D3b" is bit 1 of the LOW byte
     * of sock_$pkt_info_t.flags, i.e. bit 1 of the word: the frame arrived
     * over XNS ("standard") routing and its IDP header is already in front of
     * us, so there is no Domain internet header to parse.
     */
    is_std = ((f.pkt.flags & SOCK_PKT_FLAG_XNS) != 0) ? true : false;

    PKT_$DUMP_DATA(f.pkt.data_pages, (int16_t)f.pkt.data_len);  /* 0x00E68A30 */

    /* sock_$pkt_info_t.hdr is a target VA (sock/sock.h), not a C pointer */
    packet = (xns_$idp_header_t *)ARCH_VA_TO_PTR(f.pkt.hdr); /* 0x00E68A42 */

    /* "move.l A2,D4 / andi.w #-0x400,D4w" - only the low word is masked, but
     * bits 0..9 are all it needs to clear: the 1KB netbuf page. */
    page_base = (const uint8_t *)((uintptr_t)packet & ~(uintptr_t)0x3FF);

    if (is_std < 0) {
        /* --- 0x00E68A52-0x00E68A8C: raw IDP, no header to break down --- */
        f.status = status_$ok;                           /* 0x00E68A52 */
        f.payload_va = ARCH_PTR_TO_VA(packet) + XNS_IDP_HEADER_SIZE;

        /* 0x00E68A62-0x00E68A6E: 7 longwords plus a word = the 30-byte
         * header.  Note header_tail is NOT part of the copy. */
        f.header = *packet;

        /* 0x00E68A70-0x00E68A82: 0x87 longwords plus a word = 0x21E bytes */
        for (i = 0; i < sizeof(rip_$packet_t); i++) {
            ((uint8_t *)&f.packet_data)[i] =
                ((const uint8_t *)ARCH_VA_TO_PTR(f.payload_va))[i];
        }

        /* 0x00E68A84: the IDP length counts its own header */
        f.data_len = (uint16_t)(f.header.length - XNS_IDP_HEADER_SIZE);
    } else {
        /*
         * --- 0x00E68A8E-0x00E68AD2 ---
         * Fourteen arguments, 0x34 bytes of caller cleanup.  Argument 2 is
         * sock_$pkt_info_t.hdr_len, which PKT_$BRK_INTERNET_HDR never reads
         * (nothing in 0x00E12328-0x00E1248C touches (0xC,A6)); it is passed
         * all the same.
         */
        PKT_$BRK_INTERNET_HDR((pkt_$hdr_t *)packet, f.pkt.hdr_len,
                              &f.network, &f.dest_node, &f.dest_sock,
                              &f.src_node_or, &f.src_node, &f.src_sock,
                              f.info_out, &f.id_out,
                              &f.packet_data, sizeof(rip_$packet_t),
                              &f.data_len, &f.status);
    }

    /* 0x00E68AD2: D4 is loaded on the internet path only; the XNS arm loads
     * it from the header copy in the response arm (0x00E68C9E). */
    network = f.network;

    RIP_$STATS.packets_received++;                       /* 0x00E68AD6 */

    /* 0x00E68ADC-0x00E68B14: three chances to reject the packet */
    if (f.status != status_$ok) {
        goto bad_packet;
    }
    f.entry_count = (int16_t)(((int32_t)(int16_t)f.data_len - 2) / RIP_ENTRY_SIZE);
    if (f.status != status_$ok) {                        /* 0x00E68AF0: retested */
        goto bad_packet;
    }
    if (f.entry_count < 0) {                             /* 0x00E68AFA */
        goto bad_packet;
    }
    if (f.entry_count > RIP_MAX_ENTRIES) {               /* 0x00E68AFE */
        goto bad_packet;
    }
    /* "cmp.w D2w,D0w / sne D5b / tst.b D5b / bpl" - lengths must agree */
    if (RIP_$PACKET_LENGTH(f.entry_count) != (int16_t)f.data_len) {
        goto bad_packet;
    }

    /*
     * 0x00E68B2E-0x00E68B42: the netbuf page carries the receiving port's
     * network number at +0x3E0 and its socket at +0x3E2, the latter
     * zero-extended to a longword by "clr.l D5 / move.w (0x3e2,A3),D5w".
     */
    port_network = (uint16_t)((page_base[0x3E0] << 8) | page_base[0x3E1]);
    port_index = ROUTE_$FIND_PORT(port_network,
                                  (int32_t)(uint32_t)
                                      ((page_base[0x3E2] << 8) | page_base[0x3E3]));

    /* 0x00E68B46-0x00E68B54: the header buffer goes back either way */
    f.hdr_va = ARCH_PTR_TO_VA(packet);
    NETBUF_$RTN_HDR(&f.hdr_va);

    if (port_index == -1) {                              /* 0x00E68B56 */
        return;
    }

    /* 0x00E68B5E-0x00E68B78: dispatch on the command word */
    switch (f.packet_data.command) {
    case RIP_CMD_REQUEST:       goto arm_request;        /* 0x00E68B7C */
    case RIP_CMD_RESPONSE:      goto arm_response;       /* 0x00E68C88 */
    case RIP_CMD_NAME_REGISTER: goto arm_name_register;  /* 0x00E68DCA */
    default:                    goto unknown_command;    /* 0x00E68E14 */
    }

/* ------------------------------------------------------------------------- */
arm_request:
    /* 0x00E68B7C */
    if (is_std < 0) {
        /*
         * --- STD/XNS request (0x00E68B82-0x00E68C12) ---
         *
         * With only one STD routing port there is nothing worth answering a
         * broadcast with, so a request addressed to the IDP broadcast host
         * FF:FF:FF:FF:FF:FF is dropped.  The three word compares are on
         * header + 0x0A, +0x0C and +0x0E - the DESTINATION host - and their
         * "seq" results are ANDed, so "bmi" fires only when all three match.
         */
        if (ROUTE_$STD_N_ROUTING_PORTS <= 1) {           /* 0x00E68B82 */
            boolean all_ones =
                (boolean)(-(int)(((f.header.dest_host[0] << 8) | f.header.dest_host[1]) == 0xFFFF)
                        & -(int)(((f.header.dest_host[2] << 8) | f.header.dest_host[3]) == 0xFFFF)
                        & -(int)(((f.header.dest_host[4] << 8) | f.header.dest_host[5]) == 0xFFFF));
            if (all_ones < 0) {                          /* 0x00E68BA8 */
                return;
            }
        }

        RIP_$PROCESS_REQUEST(true, &f);                  /* 0x00E68BAC */

        /*
         * 0x00E68BB6-0x00E68C12: send the reply straight back to the IDP
         * source address (header + 0x12 = { src_network, src_host,
         * src_socket }, the 12 bytes RIP_$SEND wants), then wait; five
         * attempts, or until TIME_$WAIT reports "quit while waiting".
         *
         * "pea (-0xe,A6)" is &header + 0x12; taken as a byte address because
         * xns_$idp_header_t is packed.
         */
        retries = 0;
        do {
            retries++;                                   /* 0x00E68BBA */

            RIP_$SEND((uint8_t *)&f.header + 0x12,       /* 1 addr_info  */
                      port_index,                        /* 2 port       */
                      &f.response,                       /* 3 route_data */
                      (uint16_t)RIP_$PACKET_LENGTH(f.response_count),
                      true);                             /* 5 flags      */

            f.wait_delay.high = 0;                       /* 0x00E68BE0 */
            f.wait_delay.low  = RIP_SEND_DELAY_LOW;      /* 0x00E68BE4 */
            TIME_$WAIT(&rip_$server_wait_delay_type, &f.wait_delay,
                       &f.wait_status);                  /* 0x00E68BF6 */

            if (f.wait_status == status_$time_quit_while_waiting) {
                return;                                  /* 0x00E68C08 */
            }
        } while (retries < RIP_SEND_RETRIES);            /* 0x00E68C0C, unsigned */

        return;                                          /* 0x00E68C12 */
    }

    /*
     * --- Domain-internet request (0x00E68C16-0x00E68C84) ---
     *
     * With only one routing port, answer only if the low byte of the first
     * word of PKT_$BRK_INTERNET_HDR's info record (header byte 0x0E) is
     * negative - "tst.b (-0x2af,A6) / bmi" drops the packet when it is.
     */
    if (ROUTE_$N_ROUTING_PORTS <= 1) {                   /* 0x00E68C16 */
        if ((int8_t)(f.info_out[0] & 0xFF) < 0) {        /* 0x00E68C20 */
            return;
        }
    }

    RIP_$PROCESS_REQUEST(false, &f);                     /* 0x00E68C2C */

    f.info_out[0] = 0x20;                                /* 0x00E68C32 */

    /*
     * 0x00E68C38-0x00E68C7E: fifteen arguments plus the 2-byte Pascal result
     * slot; the addresses the packet came from become the addresses it goes
     * back to.  The frame is discarded by "unlk", so the block is never
     * popped.
     *
     * PKT_$BLD_INTERNET_HDR writes a word through BOTH arguments 13 and 14
     * unconditionally (0x00E1230E, 0x00E12316), so neither may be NULL; they
     * are two distinct word locals here as in the original.
     */
    PKT_$SEND_INTERNET(
        f.src_node_or,                  /*  1 routing_key   A6-0x4F4  */
        f.src_node,                     /*  2 dest_node     A6-0x4F8  */
        f.src_sock,                     /*  3 dest_sock     A6-0x51A  */
        (int32_t)network,               /*  4 src_node_or   D4        */
        NODE_$ME,                       /*  5 src_node      0xE245A4  */
        RIP_SOCKET,                     /*  6 src_sock      #8        */
        f.info_out,                     /*  7 pkt_info      A6-0x2B0  */
        f.id_out,                       /*  8 request_id    A6-0x518  */
        &f.response,                    /*  9 template      A6-0x290  */
        (uint16_t)RIP_$PACKET_LENGTH(f.response_count),  /* 10 template_len */
        RIP_$ANNOUNCE_EXTRA,            /* 11 data          0xE68E28  */
        0,                              /* 12 data_len                */
        &f.send_retry_hint,             /* 13               A6-0x50E  */
        &f.send_timeout,                /* 14               A6-0x50C  */
        &f.status);                     /* 15 status_ret    A6-0x4EC  */
    return;                                              /* 0x00E68C84 */

/* ------------------------------------------------------------------------- */
arm_response:
    /* 0x00E68C88-0x00E68C98: ROUTE_$PORTP is indexed from 0 here */
    port = ROUTE_$PORTP[port_index];

    if (is_std < 0) {
        /* 0x00E68C9E: the network this XNS packet was addressed to */
        network = f.header.dest_network;
    }

    /*
     * 0x00E68CA4-0x00E68D16: the port has moved to a different network.
     * "move.w (0x2c,A2),D1w / moveq #0x38,D5 / btst.l D1,D5" - ports whose
     * routing-capability bit is 3, 4 or 5 are left alone.
     */
    if (network != port->network &&
        ((0x38u >> (port->active & 0x1F)) & 1u) == 0) {

        f.source_addr.network = port->network;           /* 0x00E68CB2 */
        /* "clr.w" three times through A0 - the 6-byte host */
        f.source_addr.host[0] = 0; f.source_addr.host[1] = 0;
        f.source_addr.host[2] = 0; f.source_addr.host[3] = 0;
        f.source_addr.host[4] = 0; f.source_addr.host[5] = 0;

        /* Withdraw the old network (metric 0x10) ... 0x00E68CC6 */
        RIP_$UPDATE_INT(port->network, &f.source_addr, 0x10,
                        (uint16_t)port_index, is_std, &f.status);

        f.source_addr.network = network;                 /* 0x00E68CE4 */

        /* ... and install the new one at metric 0.  0x00E68CE8 */
        RIP_$UPDATE_INT(network, &f.source_addr, 0,
                        (uint16_t)port_index, is_std, &f.status);

        port->network          = network;                /* 0x00E68D04 */
        port->xns_addr.network = network;                /* 0x00E68D06 */

        if (port_index == 0) {                           /* 0x00E68D0A */
            HINT_$ADD_NET(port->network);                /* 0x00E68D0E */
        }
    }

    /*
     * 0x00E68D18-0x00E68D58: should the routes in this packet be believed?
     * The two arms are the compiler's short-circuit expansion of
     *   (n < 2) or ((n > 1) and (port^.active in <set>))
     * with <set> = {4,5} (0x30) on the STD side and {3,5} (0x28) otherwise.
     * The "tst.b D3b / bmi" at 0x00E68D38 is only reachable on the STD side,
     * so it always branches; it is emitted because the original does.
     */
    if (is_std < 0) {                                    /* 0x00E68D18 */
        n = ROUTE_$STD_N_ROUTING_PORTS;                  /* 0x00E68D1C */
        if (n < 2) {
            goto process_routes;                         /* 0x00E68D26 */
        }
        if (n > 1) {                                     /* 0x00E68D28 */
            if (((0x30u >> (port->active & 0x1F)) & 1u) != 0) {
                goto process_routes;                     /* 0x00E68D36 */
            }
        }
        if (is_std < 0) {                                /* 0x00E68D38 */
            goto send_updates;
        }
    }
    n = ROUTE_$N_ROUTING_PORTS;                          /* 0x00E68D3E */
    if (n < 2) {
        goto process_routes;                             /* 0x00E68D48 */
    }
    if (n <= 1) {                                        /* 0x00E68D4A */
        goto send_updates;
    }
    if (((0x28u >> (port->active & 0x1F)) & 1u) == 0) {  /* 0x00E68D50 */
        goto send_updates;
    }

process_routes:
    /* 0x00E68D5A-0x00E68D86: who the routes came from */
    f.source_addr.network = network;

    if (is_std < 0) {
        /* Three words out of header + 0x16 - the IDP source host */
        f.source_addr.host[0] = f.header.src_host[0];    /* 0x00E68D68 */
        f.source_addr.host[1] = f.header.src_host[1];
        f.source_addr.host[2] = f.header.src_host[2];
        f.source_addr.host[3] = f.header.src_host[3];
        f.source_addr.host[4] = f.header.src_host[4];
        f.source_addr.host[5] = f.header.src_host[5];
    } else {
        /*
         * 0x00E68D78-0x00E68D86: "andi.l #-0x100000,(-0x2a,A6)" then
         * "or.l D1,(-0x2a,A6)" - the LOW FOUR bytes of the 6-byte host keep
         * their top 12 bits and take the 20-bit source node id.  host[0] and
         * host[1] are not touched on this path at all.
         */
        uint32_t host_lo = ((uint32_t)f.source_addr.host[2] << 24)
                         | ((uint32_t)f.source_addr.host[3] << 16)
                         | ((uint32_t)f.source_addr.host[4] << 8)
                         |  (uint32_t)f.source_addr.host[5];
        host_lo = (host_lo & 0xFFF00000u) | f.src_node;
        f.source_addr.host[2] = (uint8_t)(host_lo >> 24);
        f.source_addr.host[3] = (uint8_t)(host_lo >> 16);
        f.source_addr.host[4] = (uint8_t)(host_lo >> 8);
        f.source_addr.host[5] = (uint8_t)host_lo;
    }

    /*
     * 0x00E68D88-0x00E68DBC: "move.w (-0x514,A6),D1w / subq.w #1,D1w / bmi"
     * then "dbf" - entry_count iterations, not entry_count - 1.  A3 walks the
     * frame from A6+6 in steps of 6, so entry i is read at packet_data + 2 +
     * 6i (network) and packet_data + 6 + 6i (metric).
     */
    if ((int16_t)(f.entry_count - 1) < 0) {
        goto send_updates;
    }
    for (n = 0; n < f.entry_count; n++) {
        RIP_$UPDATE_INT(f.packet_data.entries[n].network,
                        &f.source_addr,
                        f.packet_data.entries[n].metric,
                        (uint16_t)port_index, is_std, &f.status);
    }

send_updates:
    RIP_$SEND_UPDATES(is_std);                           /* 0x00E68DC4 */
    return;

/* ------------------------------------------------------------------------- */
arm_name_register:
    /* 0x00E68DCA */
    if (is_std < 0) {
        /*
         * 0x00E68DCE-0x00E68DFA: only IDP packet type 0xBE (header + 0x05)
         * carries a name-service registration.
         */
        if ((uint16_t)f.header.packet_type != 0xBE) {
            goto unknown_command_std;                    /* 0x00E68DFC */
        }

        f.reg_network = f.header.src_network;            /* 0x00E68DDA */

        /*
         * ORIGINAL DEFECT, PRESERVED (bead source-u9wy, confirmed instruction
         * by instruction).  0x00E68DE0 `lea (-0xa,A6),A2` points at the IDP
         * SOURCE HOST, header + 0x16, and 0x00E68DEA `and.l (0x6,A2),D1` then
         * reads the longword six bytes on - A6-0x04, i.e. header + 0x1C.
         *
         * The +6 accessor belongs to the 10-byte { network:4, host_hi:2,
         * host_lo:4 } record (rip_$nexthop_t), whose base is header + 0x12 =
         * A6-0x0E; applying it to a pointer already four bytes into that
         * record yields src_socket (header + 0x1C, two bytes) followed by
         * A6-0x02 and A6-0x01.  Those last two bytes are past the end of the
         * 30-byte header copy at 0x00E68A62-0x00E68A6E, which fills only
         * A6-0x20..A6-0x03, so they are uninitialised frame storage.
         *
         * The node id handed to the name server is therefore
         * (src_socket << 16 | two junk bytes) & 0xFFFFF.  No fix is possible
         * without changing behaviour.
         */
        f.reg_node_id = (((uint32_t)f.header.src_socket << 16)
                       | ((uint32_t)f.header_tail[0] << 8)
                       |  (uint32_t)f.header_tail[1]) & 0xFFFFF;

        /*
         * "pea (-0x538,A6)" (0x00E68DF2) then "pea (-0x4e0,A6)" (0x00E68DF6),
         * then the shared jsr.  The last push is argument 1, so this is
         * (&reg_network, &reg_node_id).  The callee reads neither and the
         * arguments are never popped - "unlk A6" at 0x00E68E1C discards them.
         */
        REM_NAME_$REGISTER_SERVER(&f.reg_network, &f.reg_node_id); /* 0x00E68E0C */
        return;                                          /* 0x00E68E12 */
    }

    /*
     * 0x00E68E04-0x00E68E12: the internet path pushes &src_node (A6-0x4F8,
     * 0x00E68E04) then &src_node_or (A6-0x4F4, 0x00E68E08), so argument 1 is
     * &src_node_or.  REM_NAME_$REGISTER_SERVER (0x00E4A4AE) reads neither - it
     * only stamps TIME_$CLOCKH into the name-server record and sets the
     * "server contacted" flag - and the arguments are never popped because
     * "unlk A6" discards them.
     */
    REM_NAME_$REGISTER_SERVER(&f.src_node_or, &f.src_node);
    return;

unknown_command_std:
    RIP_$STATS.unknown_commands++;                       /* 0x00E68DFC */
    return;

unknown_command:
    RIP_$STATS.unknown_commands++;                       /* 0x00E68E14 */
    return;

/* ------------------------------------------------------------------------- */
bad_packet:
    /* 0x00E68B16-0x00E68B2A */
    RIP_$STATS.errors++;
    f.hdr_va = ARCH_PTR_TO_VA(packet);
    NETBUF_$RTN_HDR(&f.hdr_va);
}
