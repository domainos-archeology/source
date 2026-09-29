/*
 * ROUTE_$PROCESS - the routing server process
 *
 * ROUTE_$INIT_ROUTING binds this procedure as a process (the pointer to it
 * lives at 0x00E69D50) and advances ROUTE_$CONTROL_EC when the rest of the
 * routing state is ready.  ROUTE_$PROCESS then multiplexes three event
 * counts with EC_$WAIT:
 *
 *   index 0  TIME_$CLOCKH        periodic RIP broadcast, every 0x72 ticks
 *   index 1  the routing socket  a packet needs forwarding
 *   index 2  ROUTE_$CONTROL_EC   shut the router down and unbind
 *
 * A5 in the original is 0x00E87D80, the base of the wired routing data; the
 * A5-relative operands in the comments below are resolved to their absolute
 * addresses, which route/route_internal.h names.
 *
 * Original address: 0x00E873EC (1200 bytes)
 */

#include "route/route_internal.h"
#include "ec/ec.h"
#include "proc1/proc1.h"
#include "sock/sock.h"
#include "rip/rip.h"
#include "time/time.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "net_io/net_io.h"
#include "network/network.h"
#include "mac_os/mac_os.h"
#include "xns/xns.h"
#include "wp/wp.h"
#include "uid/uid.h"
#include "ml/ml.h"
#include "ring/ringlog.h"

/*
 * Every global this function touches comes from a header:
 *   ROUTE_$*                      route/route_internal.h, route/route.h
 *   SOCK_$EVENT_COUNTERS, SOCK_$* sock/sock.h
 *   TIME_$CLOCKH                  time/time.h
 *   NODE_$ME                      uid/uid.h
 *   RING_$LOGGING_NOW             ring/ringlog.h
 *   XNS_IDP_$DATA (port table)    xns/xns.h
 */

/*
 * The constant cells this routine passes by reference.  They sit in the
 * ROUTE_ CODE segment immediately after ROUTE_$PROCESS' own `rts`
 * (0x00E8789A) and before the next routine's `link.w` (0x00E878A8), they
 * carry no symbol in the SAU2 link map, and every reference to them is
 * PC-relative from inside this function - so they are file statics here
 * rather than data-segment globals.  Image bytes at 0x00E8789C:
 *
 *   00e8789c  00 00               net_service_or_bits
 *   00e8789e  00 01               net_service_and_not_bits
 *   00e878a0  00 00 20 48         RINGLOG_$ROUTE_FORWARD (ring/ringlog.h)
 *   00e878a4  00 11 00 06         sock_empty_status
 */

/* NETWORK_$SET_SERVICE opcode 0, "or these service bits in".
 * `pea (0x45e,PC)` at 0x00E8743C. */
static const int16_t net_service_or_bits = 0x0000;      /* 0x00E8789C */

/* NETWORK_$SET_SERVICE opcode 1, "and not these service bits".
 * `pea (0x76,PC)` at 0x00E87826. */
static const int16_t net_service_and_not_bits = 0x0001; /* 0x00E8789E */

/*
 * status_$network_buffer_queue_is_empty (OS / network, code 6 - "buffer
 * queue is empty").  Handed to CRASH_SYSTEM when SOCK_$GET reports it
 * dequeued nothing; `pea (0x3e6,PC)` at 0x00E874BC.
 */
static const status_$t sock_empty_status = 0x00110006;  /* 0x00E878A4 */

void ROUTE_$PROCESS(void)
{
    ec_$wait_ecs_t          ecs;
    ec_$wait_vals_t         vals;
    int16_t                 wait_result;
    sock_$sock_t           *route_sock;      /* A6-0xE8 */
    uint32_t                next_broadcast;  /* A6-0xD8 */
    status_$t               status;          /* A6-0xD0 */
    net_io_$send_info_t     net_io_info;     /* A6-0xD4: NET_IO_$SEND's ninth arg */
    uint32_t                hdr_pa;          /* A6-0xE4 */
    uint32_t                hdr_ptr_cell[1]; /* A6-0xE0 */
    sock_$pkt_info_t        rcv;             /* A6-0xB0, 0x40 bytes */
    route_$internet_hdr_t  *pkt;             /* A4 */
    xns_$idp_header_t      *idp;             /* A2 */
    boolean                 is_std_routing;  /* D2 */
    boolean                 should_forward;  /* D3 */
    boolean                 was_forwarded;   /* D5 */
    int16_t                 hop_count;       /* D5, before it becomes the flag */
    int16_t                 stat_index;      /* D0 */
    int16_t                 queue_depth;     /* D0 */
    int16_t                 next_hop_port;   /* A6-0xEE */
    rip_$dest_addr_t        dest_addr;       /* A6-0x70, 12 bytes */
    rip_$nexthop_t          next_hop;        /* A6-0x60, 10 bytes */
    mac_os_$send_pkt_t      mac_send;        /* A6-0x50, 0x4C bytes */
    int16_t                 mac_bytes_sent;  /* A6-0xEA */
    uint16_t                closing_sock;    /* A6-0xF6 */
    route_$port_t          *port;            /* A3 */
    route_$port_stats_t    *port_stats;      /* A2 */
    int16_t                 i;
    int                     j;

    /*
     * 0x00E873FA - 0x00E87410: wait for ROUTE_$INIT_ROUTING to advance the
     * control event count, then consume that advance.
     */
    EC_$WAITN(&PTR_ROUTE_$CONTROL_EC, (int32_t *)&ROUTE_$CONTROL_ECVAL, 1);
    ROUTE_$CONTROL_ECVAL++;                                 /* 0x00E87414 */

    /*
     * 0x00E8741A - 0x00E87428: the socket table is indexed from 0xE28DB4
     * with a -4 displacement, i.e. SOCK_$EVENT_COUNTERS[sock - 1].  The
     * pointer is the socket descriptor, whose first field is its event
     * count.
     */
    route_sock = (sock_$sock_t *)SOCK_$EVENT_COUNTERS[ROUTE_$SOCK - 1];

    /* 0x00E8742E: "st (0x00E26F1E).l" - ROUTE_$ROUTING is a byte */
    ROUTE_$ROUTING = true;

    /* 0x00E87434 - 0x00E87446 */
    NETWORK_$SET_SERVICE((int16_t *)&net_service_or_bits,
                         &ROUTE_$SERVICE_ID, &status);

    /* 0x00E8744A: the value of TIME_$CLOCKH, not its address */
    next_broadcast = TIME_$CLOCKH;

    /* 0x00E87452 - 0x00E8745E */
    PROC1_$SET_LOCK(ROUTE_$PROC_LOCK_ID);

    for (;;) {
        /*
         * 0x00E87460 - 0x00E87488: both arrays go on the stack by value,
         * ecs at the lower address.  24 bytes, no result slot; the index of
         * the satisfied event count comes back in D0.
         */
        ecs.ec[0] = (ec_$eventcount_t *)&TIME_$CLOCKH;
        ecs.ec[1] = &route_sock->ec;
        ecs.ec[2] = (ec_$eventcount_t *)&ROUTE_$CONTROL_EC;

        vals.val[0] = (int32_t)next_broadcast;
        vals.val[1] = (int32_t)ROUTE_$SOCK_ECVAL;
        vals.val[2] = (int32_t)ROUTE_$CONTROL_ECVAL;

        wait_result = EC_$WAIT(ecs, vals);

        /*
         * 0x00E8748C - 0x00E874A2: the dispatch tests 1, then 0, then 2 and
         * falls back to the top of the loop for anything else.
         */
        if (wait_result == 1) {
            goto packet_received;
        }
        if (wait_result == 0) {
            goto broadcast_timer;
        }
        if (wait_result == 2) {
            goto shutdown;
        }
        continue;

    broadcast_timer:                                        /* 0x00E877C2 */
        if (ROUTE_$N_ROUTING_PORTS > 1) {
            RIP_$BROADCAST(false);                          /* 0x00E877CE: clr.w */
        }
        if (ROUTE_$STD_N_ROUTING_PORTS > 1) {
            RIP_$BROADCAST(true);                           /* 0x00E877E4: st */
        }
        /* 0x00E877EE: moveq #0x72,D1 / add.l TIME_$CLOCKH,D1 */
        next_broadcast = TIME_$CLOCKH + ROUTE_$BROADCAST_INTERVAL;
        continue;

    packet_received:                                        /* 0x00E874A4 */
        /*
         * SOCK_$GET returns true when it dequeued a packet.  A false return
         * on an event-count wakeup is a kernel inconsistency; the original
         * hands CRASH_SYSTEM the constant cell at 0x00E878A4
         * (status_$network_buffer_queue_is_empty).
         */
        if (SOCK_$GET(ROUTE_$SOCK, &rcv) >= 0) {            /* 0x00E874B8: bmi */
            CRASH_SYSTEM(&sock_empty_status);               /* 0x00E874BC */
        }

        /* sock_$pkt_info_t.hdr is a target VA (sock/sock.h), not a pointer */
        pkt = (route_$internet_hdr_t *)ARCH_VA_TO_PTR(rcv.hdr); /* 0x00E874C8 */

        /*
         * 0x00E874CC - 0x00E874E6: bucket this packet by the routing
         * socket's queue depth, capped at 0x80.
         */
        stat_index = (int16_t)route_sock->queue_count;
        if (stat_index > 0x80) {
            stat_index = 0x80;
        }
        ROUTE_$Q_DEPTH[stat_index]++;

        /*
         * 0x00E874EA - 0x00E874FE: bit 1 of the flags byte at rcv+0x11
         * selects "standard" (pure XNS) routing.  A packet is a forwarding
         * candidate when it is a standard-routing packet or when the Domain
         * internet header says routing type >= 2.
         */
        is_std_routing = (rcv.flags & 0x0002) ? true : false;
        should_forward = (boolean)(((pkt->routing_type >= 2) ? true : false) |
                                   is_std_routing);

        /* 0x00E87500 - 0x00E8750A */
        if (is_std_routing < 0) {
            idp = (xns_$idp_header_t *)pkt;
        } else {
            idp = &pkt->idp;
        }

        /*
         * 0x00E8750C - 0x00E87524: an internet packet that claims a payload
         * but carries no data pages cannot be forwarded.
         */
        if (should_forward < 0 && is_std_routing >= 0 &&
            rcv.data_pages[0] == 0 && pkt->data_len != 0) {
            ROUTE_$DLEN_ERR++;                      /* 0xE87FBC */
            should_forward = false;
        }

        /* 0x00E87526 - 0x00E8753E */
        idp->transport_ctl++;
        if (idp->checksum != 0xFFFF) {
            idp->checksum = (uint16_t)XNS_IDP_$HOP_AND_SUM(idp->checksum,
                                                           (int16_t)idp->length);
        }

        if (should_forward < 0) {                           /* 0x00E87540 */
            /* 0x00E87544 - 0x00E8755E: cmpi.w #0x10 / bcs - drop at >= 0x10 */
            hop_count = (int16_t)idp->transport_ctl;
            if (hop_count >= ROUTE_$MAX_HOP_COUNT) {
                if (is_std_routing < 0) {
                    ROUTE_$STD_TOO_FAR++;
                } else {
                    ROUTE_$TOO_FAR++;
                }
                should_forward = false;
            }

            if (should_forward < 0) {                       /* 0x00E87560 */
                /*
                 * 0x00E87564 - 0x00E87570: the 12-byte destination address
                 * is copied out of the IDP header before the lookup, which
                 * overwrites the copy with the next hop.
                 */
                dest_addr.network = idp->dest_network;
                dest_addr.host_hi = (uint16_t)((idp->dest_host[0] << 8) |
                                               idp->dest_host[1]);
                dest_addr.host_lo = ((uint32_t)idp->dest_host[2] << 24) |
                                    ((uint32_t)idp->dest_host[3] << 16) |
                                    ((uint32_t)idp->dest_host[4] << 8) |
                                    (uint32_t)idp->dest_host[5];
                dest_addr.socket  = idp->dest_socket;

                /* 0x00E87572 - 0x00E87590 */
                RIP_$FIND_NEXTHOP(&dest_addr, false, &next_hop_port,
                                  &next_hop, &status);

                if (status != status_$ok) {                 /* 0x00E87594 */
                    if (is_std_routing < 0) {
                        ROUTE_$STD_MISROUTE++;
                    } else {
                        ROUTE_$MISROUTE++;
                    }
                    should_forward = false;
                }
            }
        }

        was_forwarded = false;                              /* 0x00E875AA */

        if (should_forward < 0) {                           /* 0x00E875AC */
            /* 0x00E875B2: 0-based, muls.w port,#0x5C */
            port = &ROUTE_$PORT_ARRAY[next_hop_port];

            if (is_std_routing < 0) {
                /*
                 * 0x00E875C4: btst.l D0,#0x30 - the port's "active" word
                 * must select bit 4 or 5 for standard routing.
                 */
                if (((1u << (port->active & 0x1F)) & 0x30) == 0) {
                    ROUTE_$STD_MISROUTE++;
                    should_forward = was_forwarded;         /* move.b D5b,D3b */
                }
            } else {
                /* 0x00E875D6: btst.l D0,#0x28 - bit 3 or 5 */
                if (((1u << (port->active & 0x1F)) & 0x28) == 0) {
                    ROUTE_$MISROUTE++;
                    should_forward = was_forwarded;
                }
                /*
                 * 0x00E875E6 - 0x00E875F8: rewrite the internet header so
                 * that the packet leaves this node addressed to the next
                 * hop.  These two stores happen whether or not the port
                 * check above cleared should_forward.
                 */
                pkt->src_node = NODE_$ME;
                pkt->dest_node = next_hop.host_lo & 0xFFFFF;
            }

            if (port->port_type == ROUTE_PORT_TYPE_ROUTING) {   /* 0x00E875FA */
                /*
                 * A user routing port: hand the packet to its socket.
                 */
                if (RING_$LOGGING_NOW < 0) {                /* 0x00E87602 */
                    RINGLOG_$LOGIT(RINGLOG_$ROUTE_FORWARD, pkt);
                }

                port_stats = ROUTE_$PORT_STATS(port);

                /*
                 * 0x00E8761C - 0x00E87634.  The third argument is D5b, the
                 * was_forwarded flag, which is still false here.
                 */
                if (SOCK_$PUT(port->socket, &rcv, was_forwarded,
                              2, port->socket) < 0) {   /* 0x00E87626 pea (-0xb0,A6) */
                    was_forwarded = true;                   /* 0x00E8763C */

                    /* 0x00E8763E - 0x00E8765E */
                    queue_depth = (int16_t)route_sock->queue_count;
                    if (queue_depth > 0x20) {
                        port_stats->deep_queue_puts++;
                    } else {
                        port_stats->queue_depth[queue_depth]++;
                    }
                } else {
                    port_stats->failed_puts++;              /* 0x00E87660 */
                }

                port->forward_count++;                      /* 0x00E87664 */
                goto forward_stats;                         /* 0x00E87668 */
            }

            if (rcv.data_len <= ROUTE_$MAX_FORWARD_SIZE) {  /* 0x00E8766C */
                if (is_std_routing < 0) {                   /* 0x00E87676 */
                    /*
                     * 0x00E8767C - 0x00E87698.  MAC_OS_$ARP fills the link
                     * header at the front of the send record and sets the
                     * broadcast flag at its +0x18.
                     */
                    MAC_OS_$ARP(&next_hop, next_hop_port,
                                (uint16_t *)&mac_send,
                                (uint8_t *)&mac_send.is_broadcast, &status);

                    if (status != status_$ok) {             /* 0x00E8769C */
                        should_forward = was_forwarded;     /* 0x00E87776 */
                        goto forward_stats;
                    }

                    /*
                     * 0x00E876A4 - 0x00E876DA: finish the 0x4C-byte send
                     * record.  The one-entry descriptor chain at +0x1C
                     * describes the header buffer; the payload pages are
                     * copied straight out of the SOCK_$GET record.
                     */
                    mac_send.hdr_desc.length  = rcv.hdr_len;
                    mac_send.hdr_desc.address = (uint32_t)(uintptr_t)pkt;
                    mac_send.hdr_desc.next    = 0;
                    mac_send.hdr_prebuilt = true;
                    mac_send.frame_type   = ROUTE_$MAC_FRAME_TYPE;
                    mac_send.data_length  = rcv.data_len;
                    for (j = 0; j < 4; j++) {
                        mac_send.data_pages[j] = rcv.data_pages[j];
                    }

                    /* 0x00E876DC - 0x00E87704 */
                    MAC_OS_$SEND(XNS_IDP_$PORT_MAC_CHANNEL(next_hop_port),
                                 &mac_send, &mac_bytes_sent, &status);
                    goto forward_stats;                     /* 0x00E87708 */
                }

                /*
                 * 0x00E8770A - 0x00E87766: normal (Domain internet) routing.
                 * The physical address of the header page is kept in the
                 * last longword of the 1KB page the header lives in.
                 */
                hdr_pa = *(uint32_t *)(((uintptr_t)pkt & ~(uintptr_t)0x3FF) + 0x3FC);

                ML_$LOCK(ROUTE_$NET_IO_LOCK_ID);

                hdr_ptr_cell[0] = ARCH_PTR_TO_VA(pkt);
                NET_IO_$SEND(next_hop_port,             /* port                 */
                             hdr_ptr_cell,              /* &header VA           */
                             hdr_pa,                    /* header PA            */
                             pkt->hdr_len,              /* header length        */
                             0,                         /* data VA              */
                             rcv.data_pages,            /* payload page vector  */
                             pkt->data_len,             /* payload length       */
                             ROUTE_$FWD_TIMEOUT,        /* send flags/timeout   */
                             &net_io_info,              /* out send_info        */
                             &status);

                ML_$UNLOCK(ROUTE_$NET_IO_LOCK_ID);
                goto forward_stats;                         /* 0x00E87766 */
            }

            /* 0x00E87768 - 0x00E87776: too big to put on a real port */
            if (is_std_routing < 0) {
                ROUTE_$STD_DLEN_ERR++;
            } else {
                ROUTE_$DLEN_ERR++;
            }
            should_forward = was_forwarded;
        }

    forward_stats:                                          /* 0x00E87778 */
        /*
         * 0x00E87778 - 0x00E8778E.  The second "tst.b D2b" at 0x00E87786 is
         * unreachable-as-taken (D2 was already known negative), so this is
         * simply: count a forward against the matching bucket.
         */
        if (is_std_routing < 0 && should_forward < 0) {
            ROUTE_$STD_PKTS_ROUTED++;
        } else if (should_forward < 0) {
            ROUTE_$PKTS_ROUTED++;
        }

        /*
         * 0x00E87792 - 0x00E877B6: nothing took ownership of the buffers,
         * so give them back.  NETBUF_$RTN_HDR is handed a copy of the
         * header pointer, not the SOCK_$GET record.
         */
        if (was_forwarded >= 0) {
            hdr_ptr_cell[0] = ARCH_PTR_TO_VA(pkt);
            NETBUF_$RTN_HDR(hdr_ptr_cell);
            PKT_$DUMP_DATA(rcv.data_pages, (int16_t)rcv.data_len);
        }

        ROUTE_$SOCK_ECVAL++;                                /* 0x00E877B8 */
        continue;

    shutdown:                                               /* 0x00E877FE */
        ROUTE_$CONTROL_ECVAL++;
        PROC1_$CLR_LOCK(ROUTE_$PROC_LOCK_ID);

        ROUTE_$ROUTING = false;                             /* clr.b, 0x00E87812 */
        ROUTE_$LAST_UPDATE_TIME = 0;                        /* 0x00E87818 */

        NETWORK_$SET_SERVICE((int16_t *)&net_service_and_not_bits,
                             &ROUTE_$SERVICE_ID, &status);

        /* 0x00E87834 - 0x00E87850 */
        closing_sock = ROUTE_$SOCK;
        ROUTE_$SOCK = 0xFFFF;
        SOCK_$CLOSE(closing_sock);

        ROUTE_$NETBUF_ALLOC = 0;                           /* 0x00E87852 */

        /*
         * 0x00E87856 - 0x00E8787E: with no user ports left, release the
         * wired pages.  "moveq D0 = n-1; bmi skip; lea (0x4,A5),A2;
         * ... move.l (-0x4,A2) ... addq.l #4,A2; dbf" walks the array
         * upwards from index 0.
         */
        if (ROUTE_$N_USER_PORTS == 0) {
            for (i = 0; i < ROUTE_$N_WIRED_PAGES; i++) {
                WP_$UNWIRE(ROUTE_$WIRED_PAGES[i]);
            }
            ROUTE_$N_WIRED_PAGES = 0;
        }

        /* 0x00E87882 - 0x00E8788C: the process does not return from here */
        PROC1_$UNBIND(ROUTE_$PID, &status);
        return;
    }
}
