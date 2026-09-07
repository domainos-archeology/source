/*
 * XNS IDP Demultiplexing
 *
 * Implementation of packet demultiplexing for incoming IDP packets.
 *
 * Original addresses:
 *   XNS_IDP_$OS_DEMUX:          0x00E184A8
 *   XNS_IDP_$DEMUX:             0x00E18B8A
 *   XNS_IDP_$OS_ADD_PORT:       0x00E1872C
 *   XNS_IDP_$OS_DELETE_PORT:    0x00E1876C
 *
 * All four routines use A5 = 0xE2B314 (the IDP module data base,
 * `lea (0xE2B314).l,A5'), reached here through XNS_IDP_BASE.
 */

#include "xns/xns_internal.h"

/*
 * Constant cells in the code region, passed to XNS_ERROR_$SEND by reference.
 *
 * Apollo Pascal passes literal `const' arguments by address: the compiler
 * places the literal in the code stream and emits `pea (d,PC)'.  The three
 * cells used by XNS_IDP_$OS_DEMUX are:
 *
 *   0x00E18726  word 0x0000  error parameter (both branches)
 *   0x00E18728  word 0x0201  bad checksum detected in transit
 *   0x00E1872A  word 0x0001  bad checksum detected at the destination
 *
 * `pea (0x1DC,PC)' at 0x00E1854C resolves to 0x00E1854C + 2 + 0x1DC =
 * 0x00E1872A; `pea (0x1C8,PC)' at 0x00E1855E resolves to 0x00E18728; both
 * `pea (0x1DC,PC)' at 0x00E18548 and `pea (0x1CA,PC)' at 0x00E1855A resolve
 * to 0x00E18726.  The cells are read-only, hence `const'; the explicit cast
 * at the call site only drops the qualifier to match the callee prototype.
 */
static const uint16_t xns_idp_c_error_param_none = XNS_ERROR_PARAM_NONE;
static const uint16_t xns_idp_c_bad_checksum_transit =
    XNS_ERROR_BAD_CHECKSUM_TRANSIT;
static const uint16_t xns_idp_c_bad_checksum_at_dest = XNS_ERROR_BAD_CHECKSUM;

#define XNS_IDP_CONST_REF(c) ((uint16_t *)&(c))

/*
 * Is a six-byte XNS host id the all-ones (broadcast) host?
 *
 * The original compares the three constituent words against -1 with
 * `cmp.w' (XNS_IDP_$OS_DEMUX 0x00E184D0..0x00E184E0 on the source host,
 * XNS_IDP_$DEMUX 0x00E18BB2..0x00E18BC2 on the destination host).  Testing
 * the six bytes is equivalent and independent of host byte order.
 */
static int xns_idp_host_is_all_ones(const uint8_t host[6])
{
    return host[0] == 0xFF && host[1] == 0xFF && host[2] == 0xFF &&
           host[3] == 0xFF && host[4] == 0xFF && host[5] == 0xFF;
}

/*
 * XNS_IDP_$OS_DEMUX - Demultiplex an incoming IDP packet (OS level)
 *
 * Installed as the MAC-layer receive callback by xns_$add_port
 * (`move.l #0xE184A8,(-0x10,A6)' at 0x00E17C40).  The routine:
 *
 *   1. counts the packet and rejects frames with a broadcast source host;
 *   2. verifies the IDP checksum, replying with an XNS Error Protocol
 *      packet when it is wrong;
 *   3. if the IDP destination address is one of ours (or broadcast),
 *      finds the channel bound to the destination socket and calls its
 *      demux vector;
 *   4. otherwise, if this node routes standard IDP traffic and the packet
 *      still has hops left, queues it on the routing socket.
 *
 * Domain Pascal single-exit: every path leaves through `done', which maps
 * to the shared epilogue at 0x00E1871C.
 *
 * @param pkt            MAC receive descriptor (A6+0x08)
 * @param port_ptr       ROUTE port index the frame arrived on (A6+0x0C)
 * @param mac_broadcast  Domain boolean, passed straight through to the
 *                       channel demux vector (A6+0x10)
 * @param status_ret     Output status (A6+0x14)
 *
 * Original address: 0x00E184A8
 */
void XNS_IDP_$OS_DEMUX(xns_$mac_rcv_t *pkt, int16_t *port_ptr,
                       boolean *mac_broadcast, status_$t *status_ret)
{
    xns_$idp_header_t *header;      /* A2 */
    route_$port_t *rport;           /* D2 */
    xns_$channel_t *chan;           /* A2, reloaded at 0x00E185EE */
    uint16_t chan_idx;              /* A6-0x98 */
    int16_t i;
    int8_t checksum_bad;            /* D2b, set by `sne' at 0x00E184FC */
    int8_t put_ok;                  /* D0b */
    uint16_t *error_code;           /* the code cell chosen at 0x00E1853E */

    /*
     * Argument record for the channel demux vector and for
     * XNS_ERROR_$SEND, built at A6-0x88 and passed by `pea (-0x88,A6)'.
     */
    xns_$pkt_desc_t rec;

    /* Argument record for SOCK_$PUT, built at A6-0x40. */
    xns_$sock_pkt_t fwd;

    /* XNS_ERROR_$SEND out-parameters: A6-0x96 (word) and A6-0x8C (status) */
    uint16_t err_result;
    status_$t err_status;

    *status_ret = status_$ok;               /* 0x00E184C0 clr.l (A0) */
    XNS_PACKETS_RECV() += 1;                /* 0x00E184C2 addq.l #1,(0x4,A5) */

    header = pkt->d.header;                 /* 0x00E184CC movea.l (0x20,A1),A2 */

    /*
     * 0x00E184D0..0x00E184E0: a frame whose IDP source host is the
     * all-ones broadcast id is not deliverable and not answerable.
     */
    if (xns_idp_host_is_all_ones(header->src_host)) {
        XNS_PACKETS_DROP() += 1;            /* 0x00E184E2 addq.l #1,(0x8,A5) */
        goto no_route;                      /* 0x00E184E6 bra.w 0x00E18684 */
    }

    /* 0x00E184EA: 0xFFFF in the checksum field means "not checksummed". */
    if (header->checksum != 0xFFFF) {
        /* 0x00E184F2..0x00E184FC: bsr xns_$get_checksum; cmp.w (A2); sne */
        checksum_bad =
            (xns_$get_checksum(pkt) != (int16_t)header->checksum) ? -1 : 0;

        /* 0x00E184FE tst.b D2b / bpl: Domain booleans are tested signed. */
        if (checksum_bad < 0) {
            /*
             * 0x00E18502..0x00E1852E: build the error-report record.  The
             * dbf loop is `moveq #0x4' + `dbf', i.e. five longwords = 20
             * bytes from pkt+0x38 into rec+0x34.
             */
            rec.mac_src_hi = pkt->d.mac_src_hi;   /* 0x00E18504 */
            rec.mac_src_lo = pkt->d.mac_src_lo;   /* 0x00E1850A */
            rec.data_len = pkt->d.data_len;       /* 0x00E18518 */
            rec.header = pkt->d.header;           /* 0x00E1851A */
            rec.iov = pkt->d.iov;                 /* 0x00E1851C */
            rec.from_net = true;                  /* 0x00E1851E st (-0x64,A6) */

            rec._unknown_34 = pkt->d._unknown_34;       /* 0x00E1852C, */
            rec.port_info = pkt->d.port_info;           /* five longwords */
            for (i = 0; i < 16; i++) {
                rec.mac_info[i] = pkt->d.mac_info[i];
            }

            /*
             * 0x00E18532..0x00E1855E: two DISTINCT error numbers.  If the
             * IDP destination address is ours (or broadcast) the checksum
             * failed at the destination (0x0001); otherwise the packet was
             * only passing through and the error is reported as detected in
             * transit (0x0201).
             */
            if (xns_$is_broadcast_addr(&header->dest_network) < 0) {
                error_code = XNS_IDP_CONST_REF(xns_idp_c_bad_checksum_at_dest);
            } else {
                error_code = XNS_IDP_CONST_REF(xns_idp_c_bad_checksum_transit);
            }

            /* 0x00E18562 pea (-0x88,A6); 0x00E18566 jsr XNS_ERROR_$SEND */
            XNS_ERROR_$SEND(&rec, error_code,
                            XNS_IDP_CONST_REF(xns_idp_c_error_param_none),
                            &err_result, &err_status);

            XNS_PACKETS_DROP() += 1;                    /* 0x00E1856C */
            *status_ret = status_$xns_bad_checksum;     /* 0x00E18572 */
            goto done;                                  /* 0x00E18578 */
        }
    }

    /* 0x00E1857C..0x00E1858A: rport = ROUTE_$PORTP[*port_ptr] */
    rport = ROUTE_$PORTP[*port_ptr];

    /* 0x00E1858E..0x00E1859A: is the IDP destination ours (or broadcast)? */
    if (xns_$is_broadcast_addr(&header->dest_network) < 0) {
        /*
         * Local delivery.
         * 0x00E1859E..0x00E185AC: socket 0xFFFF and socket 0 are not
         * deliverable.
         */
        if (header->dest_socket == 0xFFFF || header->dest_socket == 0) {
            goto drop_no_route;             /* beq.w 0x00E1867E */
        }

        /*
         * 0x00E185B0..0x00E185D4: linear scan of the 16 channels for one
         * bound to this socket.  `moveq #0xF' + `dbf' is 16 iterations and
         * the stride is `lea (0x48,A1),A1'.  chan_idx stays at 0x10 when
         * nothing matches.
         */
        chan_idx = XNS_MAX_CHANNELS;        /* 0x00E185B0 move.w #0x10 */
        for (i = 0; i < XNS_MAX_CHANNELS; i++) {
            if (XNS_CHANNEL_PTR(i)->xns_socket == (int16_t)header->dest_socket) {
                chan_idx = (uint16_t)i;     /* 0x00E185C8 */
                break;                      /* 0x00E185CC bra.b */
            }
        }

        /*
         * 0x00E185D8..0x00E185F6: no channel, or the channel has no demux
         * vector installed.  Same drop epilogue as 0x00E1867E.
         */
        if (chan_idx == XNS_MAX_CHANNELS ||
            XNS_CHANNEL_PTR(chan_idx)->demux == NULL) {
            goto drop_no_route;             /* 0x00E185F8 */
        }

        chan = XNS_CHANNEL_PTR(chan_idx);   /* 0x00E185EE lea (0,A5,D5),A2 */

        /*
         * 0x00E18608..0x00E1863C: build the callback record.  Same shape as
         * the error record above, plus the channel back-pointer at +0x30
         * (`lea (0xA0,A2),A4'), and again five longwords = 20 bytes.
         */
        rec.mac_src_hi = pkt->d.mac_src_hi;     /* 0x00E1860A */
        rec.mac_src_lo = pkt->d.mac_src_lo;     /* 0x00E18610 */
        rec.channel = chan;                     /* 0x00E1861A */
        rec.data_len = pkt->d.data_len;         /* 0x00E18626 */
        rec.header = pkt->d.header;             /* 0x00E18628 */
        rec.iov = pkt->d.iov;                   /* 0x00E1862A */
        rec.from_net = true;                    /* 0x00E1862C st (-0x64,A6) */

        rec._unknown_34 = pkt->d._unknown_34;
        rec.port_info = pkt->d.port_info;
        for (i = 0; i < 16; i++) {
            rec.mac_info[i] = pkt->d.mac_info[i];
        }

        /*
         * 0x00E18640..0x00E18658: five longword arguments, then
         * `movea.l (0xA0,A2),A4' / `jsr (A4)'.  mac_broadcast is passed on
         * as the caller's pointer (`move.l (0x10,A6),-(SP)').
         */
        ((xns_$demux_fn_t)chan->demux)(&rec, &rport->port_type,
                                       &rport->socket, mac_broadcast,
                                       status_ret);

        /*
         * 0x00E1865E..0x00E1866A: on failure count one drop and keep the
         * status the callback produced - it is NOT overwritten.
         */
        if (*status_ret == status_$ok) {
            goto done;                      /* 0x00E18662 beq.w */
        }
        XNS_PACKETS_DROP() += 1;            /* 0x00E18666 */
        goto done;                          /* 0x00E1866A bra.w */
    } else {
        /*
         * Forwarding.
         * 0x00E1866E: this node must be configured for standard IDP
         * routing (at least two routing ports).
         */
        if (ROUTE_$STD_N_ROUTING_PORTS < 2) {
            ROUTE_$STAT_DROPPED_STD_ROUTE += 1;     /* 0x00E18678 */
            goto drop_no_route;                     /* falls into 0x00E1867E */
        }

        /*
         * 0x00E1868E..0x00E18698: transport control byte compared
         * *unsigned* against 15 (`bcs' = branch if lower).
         */
        if (header->transport_ctl >= 15) {
            ROUTE_$STAT_DROPPED_STD_HOP += 1;               /* 0x00E1869A */
            XNS_PACKETS_DROP() += 1;                        /* 0x00E186A0 */
            *status_ret = status_$xns_hop_count_exceeded;   /* 0x00E186A6 */
            goto done;                                      /* 0x00E186AC */
        }

        /* 0x00E186AE..0x00E186E8: build the SOCK_$PUT record at A6-0x40. */
        fwd.flags = XNS_SOCK_PKT_F_IDP;         /* 0x00E186AE move.w #2 */
        fwd.mac_src_hi = pkt->d.mac_src_hi;     /* 0x00E186B6 */
        fwd.mac_src_lo = pkt->d.mac_src_lo;     /* 0x00E186BC */

        /*
         * 0x00E186C2 `move.l (0x30,A0),(-0x34,A6)': one longword read that
         * spans the two words at descriptor +0x2C and +0x2E.  Expressed
         * with shifts so it does not depend on host byte order.
         */
        fwd.data_len = ((uint32_t)pkt->d.pkt_len << 16) |
                       (uint32_t)pkt->d._unknown_2e;

        fwd.header = header;                    /* 0x00E186C8 move.l A2 */

        /* 0x00E186CC `move.w (0x1E,A0)': low word of the length at +0x1C. */
        fwd.header_len = (uint16_t)pkt->d.data_len;

        fwd.port_info = pkt->d.port_info;       /* 0x00E186D2 */

        /* 0x00E186E0..0x00E186E6: four `move.l' = 16 bytes, not 32. */
        for (i = 0; i < 16; i++) {
            fwd.mac_info[i] = pkt->d.mac_info[i];
        }

        fwd.reserved_12 = 0;                    /* 0x00E186E8 clr.w (-0x2E,A6) */

        /*
         * 0x00E186EC..0x00E18708: SOCK_$PUT(ROUTE_$SOCK, &fwd, 0,
         * rport->port_type, rport->socket) - the two event-count words are
         * pushed from (0x30,A1) then (0x2E,A1), so port_type is the first
         * of them.
         */
        /* The queue is generic: every producer builds its own 0x40-byte
         * record, and XNS's is xns_$sock_pkt_t rather than sock's
         * sock_$pkt_info_t, so the argument is passed as an opaque record
         * address. */
        put_ok = SOCK_$PUT(ROUTE_$SOCK, (void *)&fwd, 0,
                           rport->port_type, rport->socket);

        /*
         * 0x00E1870C `tst.b D0b' / `bmi': SOCK_$PUT returns the Domain
         * boolean 0xFF when the packet was queued.  Success leaves
         * *status_ret at status_$ok.
         */
        if (put_ok < 0) {
            goto done;                          /* 0x00E1870E bmi.b */
        }

        XNS_PACKETS_DROP() += 1;                        /* 0x00E18710 */
        *status_ret = status_$xns_packet_dropped;       /* 0x00E18716 */
        goto done;
    }

drop_no_route:
    XNS_PACKETS_DROP() += 1;                    /* 0x00E1867E addq.l #1,(0x8,A5) */
no_route:
    *status_ret = status_$xns_no_client_for_packet;         /* 0x00E18684 move.l #0x3B0010 */
done:
    /* 0x00E1871C: shared movem/unlk/rts epilogue. */
    return;
}

/*
 * XNS_IDP_$DEMUX - Channel demux vector for user-mode channels
 *
 * Installed into xns_$channel_t.demux by XNS_IDP_$OPEN, and therefore
 * called from XNS_IDP_$OS_DEMUX at 0x00E18658 with the record that routine
 * built at A6-0x88.  It repackages the packet into the 0x40-byte socket
 * record and queues it on the channel's user socket.
 *
 * @param rec            Packet descriptor from XNS_IDP_$OS_DEMUX (A6+0x08)
 * @param port_type      &route_$port_t.port_type of the receiving port (A6+0x0C)
 * @param port_socket    &route_$port_t.socket of the receiving port (A6+0x10)
 * @param mac_broadcast  Domain boolean from the MAC layer (A6+0x14)
 * @param status_ret     Output status (A6+0x18)
 *
 * Original address: 0x00E18B8A
 */
void XNS_IDP_$DEMUX(xns_$pkt_desc_t *rec, uint16_t *port_type,
                    uint16_t *port_socket, boolean *mac_broadcast,
                    status_$t *status_ret)
{
    xns_$idp_header_t *header;      /* D0 / A1 */
    xns_$channel_t *chan;           /* A3 */
    xns_$sock_pkt_t out;            /* A6-0x40 */
    int16_t i;
    int8_t put_ok;                  /* D0b */

    *status_ret = status_$ok;               /* 0x00E18BA0 clr.l (A2) */

    /* 0x00E18BA2: the flags word at record +0x10 starts out as "IDP". */
    out.flags = XNS_SOCK_PKT_F_IDP;

    header = rec->header;                   /* 0x00E18BA8 move.l (0x1C,A0),D0 */

    /*
     * 0x00E18BB2..0x00E18BC4: destination host is the all-ones broadcast
     * id.  `bset.b #0,(-0x2F,A6)' addresses the LOW byte of the word at
     * A6-0x30, so this is bit 0 of the flags word.
     */
    if (xns_idp_host_is_all_ones(header->dest_host)) {
        out.flags |= XNS_SOCK_PKT_F_BROADCAST;
    }

    /*
     * 0x00E18BCA..0x00E18BD2: the MAC-level broadcast boolean, again the
     * low byte of the flags word (`bset.b #2,(-0x2F,A6)').
     */
    if (*mac_broadcast < 0) {
        out.flags |= XNS_SOCK_PKT_F_MAC_BCAST;
    }

    out.mac_src_hi = rec->mac_src_hi;       /* 0x00E18BD8 */
    out.mac_src_lo = rec->mac_src_lo;       /* 0x00E18BDE */

    /*
     * 0x00E18BE4..0x00E18BEA: `clr.l D1; move.w (0x2C,A0),D1w; move.l D1'
     * - a zero-extended WORD read, not a longword read.
     */
    out.data_len = (uint32_t)rec->pkt_len;

    out.header = header;                    /* 0x00E18BEE */

    /* 0x00E18BF2 `move.w (0x1A,A0)': low word of the length at +0x18. */
    out.header_len = (uint16_t)rec->data_len;

    out.port_info = rec->port_info;         /* 0x00E18BF8 */

    /* 0x00E18C06..0x00E18C0C: four `move.l' = 16 bytes, not 32. */
    for (i = 0; i < 16; i++) {
        out.mac_info[i] = rec->mac_info[i];
    }

    out.reserved_12 = 0;                    /* 0x00E18C0E clr.w (-0x2E,A6) */

    chan = rec->channel;                    /* 0x00E18C12 movea.l (0x30,A0),A3 */

    /* 0x00E18C16: no user socket bound to this channel. */
    if (chan->user_socket == XNS_NO_SOCKET) {
        *status_ret = status_$xns_no_client_for_packet; /* 0x00E18C1E; no drop counted */
        goto done;                          /* 0x00E18C24 */
    }

    /*
     * 0x00E18C26..0x00E18C3C: SOCK_$PUT(chan->user_socket, &out, 0,
     * *port_type, *port_socket).
     */
    put_ok = SOCK_$PUT(chan->user_socket, (void *)&out, 0,
                       *port_type, *port_socket);

    /* 0x00E18C46 `tst.b D0b' / `bmi': queued, leave *status_ret ok. */
    if (put_ok < 0) {
        goto done;
    }

    XNS_PACKETS_DROP() += 1;                    /* 0x00E18C4A */
    *status_ret = status_$xns_packet_dropped;   /* 0x00E18C4E */

done:
    /* 0x00E18C54: shared movem/unlk/rts epilogue. */
    return;
}

/*
 * XNS_IDP_$OS_ADD_PORT - Add a port to a channel (OS-level)
 *
 * Thin locked wrapper around xns_$add_port; the arguments are pushed as
 * (*channel, *port, status_ret) at 0x00E18744..0x00E18754.
 *
 * @param channel_ptr   Pointer to channel number
 * @param port_ptr      Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1872C
 */
void XNS_IDP_$OS_ADD_PORT(uint16_t *channel_ptr, uint16_t *port_ptr,
                          status_$t *status_ret)
{
    ML_$EXCLUSION_START(XNS_LOCK_PTR());     /* 0x00E1873C */
    xns_$add_port(*channel_ptr, (int16_t)*port_ptr, status_ret);
    ML_$EXCLUSION_STOP(XNS_LOCK_PTR());      /* 0x00E1875E */
}

/*
 * XNS_IDP_$OS_DELETE_PORT - Delete a port from a channel (OS-level)
 *
 * @param channel_ptr   Pointer to channel number
 * @param port_ptr      Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1876C
 */
void XNS_IDP_$OS_DELETE_PORT(uint16_t *channel_ptr, uint16_t *port_ptr,
                             status_$t *status_ret)
{
    ML_$EXCLUSION_START(XNS_LOCK_PTR());     /* 0x00E1877C */
    xns_$delete_port(*channel_ptr, (int16_t)*port_ptr, status_ret);
    ML_$EXCLUSION_STOP(XNS_LOCK_PTR());      /* 0x00E1879E */
}
