/*
 * XNS IDP Internal Helper Functions
 *
 * Implementation of internal helper functions used by the XNS IDP module.
 *
 * Original addresses:
 *   xns_$find_socket:          0x00E17D12
 *   xns_$add_port:             0x00E17BF8
 *   xns_$delete_port:          0x00E17CB2
 *   xns_$get_checksum:         0x00E17D46
 *   xns_$is_broadcast_addr:    0x00E17E88
 *   xns_$is_local_addr:        0x00E17850
 *
 * xns_$copy_packet_data (0x00E18C5E) used to live here as an empty stub.
 * It is a NESTED PROCEDURE of XNS_IDP_$RECEIVE, so it now sits in
 * xns/idp_receive.c as a static function taking the parent frame
 * explicitly (bead source-pvhv).
 *
 * Module data through XNS_IDP_$DATA: Claude Opus 5.5 (source-iq58).
 * xns_$get_checksum and xns_$is_broadcast_addr re-emitted from the
 * disassembly: Claude Fable 5.1 (source-tydd).
 */

#include "xns/xns_internal.h"

/*
 * xns_$find_socket - Check if a socket number is already in use
 *
 * Scans all active channels to find if the given socket number
 * is already bound to an active channel.
 *
 * @param socket    Socket number to check
 *
 * @return 0xFF (-1 as signed char) if found (in use), 0 if not found (available)
 *
 * Original address: 0x00E17D12
 */
int8_t xns_$find_socket(int16_t socket)
{
    int16_t i;

    for (i = 0; i < XNS_MAX_CHANNELS; i++) {
        xns_$channel_t *chan = &XNS_IDP_$DATA.channels[i];

        /* Check if channel is active (bit 15 set in state) */
        if (chan->state >= 0) {
            continue;  /* Not active */
        }

        /* Check if socket matches */
        if (chan->xns_socket == socket) {
            return -1;  /* Found - socket is in use */
        }
    }

    return 0;  /* Not found - socket is available */
}

/*
 * xns_$add_port - Add a port to a channel's port list
 *
 * Adds the specified port to the channel's active port list.
 * If this is the first channel using this port, opens the MAC layer.
 *
 * @param channel       Channel index
 * @param port          Port number (0-7)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17BF8
 */
void xns_$add_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    /* A3 = A5 + port * 0xC, A0 = A5 + channel * 0x48 */
    xns_$port_state_t *pstate = &XNS_IDP_$DATA.ports[port];
    xns_$channel_t *chan = &XNS_IDP_$DATA.channels[channel];

    *status_ret = status_$ok;

    /* Check if port is already open at the port level */
    if (pstate->refcount == 0) {
        /* Port not yet open - need to open MAC layer */

        /* Check if port type supports opening */
        /* Access ROUTE port state to check type */
        {
            /*
             * Port status word at +0x2C of ROUTE_$PORT_ARRAY[port] (0xE2E0A0):
             *   00e17c30    move.w (0x2c,A0,D1*0x1),D0w
             *   00e17c34    btst.l D0,D4             ; D4 = 3
             */
            uint16_t port_status = ROUTE_$PORT_ARRAY[port].active;
            if ((1 << (port_status & 0x1F)) & 0x3) {
                *status_ret = status_$internet_network_port_not_open;
                return;
            }
        }

        /* Open MAC layer for this port */
        {
            mac_os_$open_params_t mac_open_params;      /* A6-0x60 */

            /* 0x00E17C40 - 0x00E17C56 */
            mac_open_params.callback = (void *)XNS_IDP_$OS_DEMUX;
            mac_open_params.num_pkt_types = 1;
            mac_open_params.u.pkt_types[0].range_low  = 0x600;
            mac_open_params.u.pkt_types[0].range_high = 0x600;

            /* 0x00E17C64: pea (0xa,A6) - the port parameter's own slot */
            MAC_OS_$OPEN(&port, &mac_open_params, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }

            /* 0x00E17C76 / 0x00E17C7C: the two results share entry 0 */
            pstate->mac_socket = mac_open_params.u.result.channel;
            pstate->mac_handle = mac_open_params.u.result.mtu;
        }
    }

    /* Check if already added to this channel */
    if (*status_ret == status_$ok) {
        /* 0x00E17C9A "tst.b (0xdc,A0)" / bmi: a Domain boolean, tested
         * signed (the byte view used to be tested unsigned, always true) */
        if ((int8_t)chan->port_active[port] >= 0) {
            /* Not yet active for this channel - add it */
            chan->port_active[port] = 0xFF;  /* st (0xdc,A0) */
            pstate->refcount += 1;           /* addq.w #1,(0x4a,A3) */
        }
    }
}

/*
 * xns_$delete_port - Remove a port from a channel's port list
 *
 * Removes the specified port from the channel's active port list.
 * If this was the last channel using this port, closes the MAC layer.
 *
 * @param channel       Channel index
 * @param port          Port number (0-7)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17CB2
 */
void xns_$delete_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    xns_$port_state_t *pstate = &XNS_IDP_$DATA.ports[port];    /* A3 */

    *status_ret = status_$ok;

    /* Clear port active flag for this channel: clr.b (0xdc,A0) */
    XNS_IDP_$DATA.channels[channel].port_active[port] = 0;

    /* Decrement port reference count: subq.w #1,(0x4a,A3) / bne */
    pstate->refcount--;

    /* If no more references, close MAC layer */
    if (pstate->refcount == 0) {
        MAC_OS_$CLOSE((int16_t *)&pstate->mac_socket, status_ret); /* pea (0x48,A3) */
        pstate->mac_socket = 0xFFFF;                      /* move.w #-0x1 */
    }
}

/*
 * xns_$get_checksum - compute the IDP checksum over a packet descriptor
 *
 * Re-emitted from the disassembly (source-tydd).  The argument is the
 * mac_os_$send_pkt_t-shaped record both callers hold: XNS_IDP_$OS_SEND's
 * send record (0x00E18438) and the receive descriptor XNS_IDP_$OS_DEMUX is
 * handed (0x00E184F2; mac_os/mac_os.h: "the same Pascal record as
 * mac_os_$send_pkt_t").  It reads +0x1C/+0x20/+0x24 (the header buffer
 * chain), +0x38 (the payload byte count) and +0x3C.. (the payload pages).
 *
 * Register roles (0x00E17D46-0x00E17E86):
 *   A2  the record            A3  the IDP header (rec->hdr_desc.address)
 *   D2  bytes accounted for   D4  the running sum (word)
 *   A4  the descriptor chain, then A2+4+4*(page-1) for the page test
 *   D3  page number 1..4      D5  dbf count (four pages)
 *   D7  bytes left in this page, clamped to 0x400
 *
 * Every word count is a signed 32-bit divide by two ("subq/addq, bpl,
 * addq, asr.l #1" - C's truncating `/ 2') taken as a word, and every
 * length check is "idp length + 1 < accounted bytes" (cmp.l / blt).
 *
 * Two things the image does that a reader would not expect, kept as they
 * are:
 *   - 0x00E17E32 pushes rec->data_pages[0] into NETBUF_$GETVA for EVERY
 *     page, although the page test at 0x00E17E08 walks data_pages[page-1];
 *     an IDP packet fits in one 0x400-byte page, so no image path reaches
 *     the second.
 *   - the "nothing left in this page" exits at 0x00E17E10 and 0x00E17E16
 *     branch to the epilogue (0x00E17E7E) PAST the "move.w D4w,D0w" at
 *     0x00E17E7C, so the function result is whatever the last
 *     XNS_IDP_$CHECKSUM call left in D0 - the last partial sum, not the
 *     total.  A payload shorter than the four pages (every IDP packet)
 *     reaches such a page, so a packet with a payload yields its last
 *     page's partial and the header's sum is dropped; only a payload-less
 *     packet, or one filling all four pages, returns D4.  Both ends of a
 *     Domain link run this code, so they agree.  `last' carries D0.
 *
 * @param packet_info   mac_os_$send_pkt_t or xns_$mac_rcv_t
 *
 * @return the checksum, or -1 when the packet is malformed
 *
 * Original address: 0x00E17D46
 */
int16_t xns_$get_checksum(void *packet_info)
{
    const mac_os_$send_pkt_t *rec = (const mac_os_$send_pkt_t *)packet_info; /* A2 */
    xns_$idp_header_t *hdr;             /* A3 */
    int32_t  total;                     /* D2 */
    int32_t  words;                     /* D0 before each XNS_IDP_$CHECKSUM */
    uint16_t sum;                       /* D4 */
    uint16_t last = 0;                  /* D0 after the last XNS_IDP_$CHECKSUM */
    uint32_t next;                      /* A4 (chain walk) */
    int16_t  page;                      /* D3 */
    int32_t  remaining;                 /* D7 */
    int32_t  chunk;                     /* D6 */
    uint32_t va;                        /* A6-0x0C */
    status_$t status;                   /* A6-0x08 */

    /* 0x00E17D52-0x00E17D64: the header buffer must fit the IDP length */
    hdr = (xns_$idp_header_t *)ARCH_VA_TO_PTR(rec->hdr_desc.address);
    total = rec->hdr_desc.length;
    if ((int32_t)(int16_t)hdr->length + 1 < total) {
        return -1;                                              /* 0x00E17E46 */
    }

    /* 0x00E17D68-0x00E17D8C: sum the header from its length word on
     * ("lea (0x2,A3),A0"), (length - 1) / 2 words */
    words = (total - 1) / 2;
    last = XNS_IDP_$CHECKSUM((uint16_t *)((uint8_t *)hdr + 2), (int16_t)words);
    sum = last;

    /* 0x00E17D8E-0x00E17DD2: the rest of the header chain, {length,
     * address, next} descriptors until next == 0 ("cmpa.w #0,A4") */
    next = rec->hdr_desc.next;
    while (next != 0) {
        const mac_os_$buf_desc_t *elem =
            (const mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(next);

        total += elem->length;                                  /* 0x00E17D9E */
        if ((int32_t)(int16_t)hdr->length + 1 < total) {
            return -1;                                          /* 0x00E17DA6 */
        }
        words = (elem->length + 1) / 2;                         /* 0x00E17DAA */
        last = XNS_IDP_$CHECKSUM((uint16_t *)ARCH_VA_TO_PTR(elem->address),
                                 (int16_t)words);
        sum += last;                                            /* 0x00E17DC8 */
        next = elem->next;                                      /* 0x00E17DCA */
    }

    /* 0x00E17DD4: no payload - the total is the result */
    if ((int32_t)rec->data_length <= 0) {
        return (int16_t)sum;                                    /* 0x00E17E7C */
    }

    /* 0x00E17DDC-0x00E17E78: "moveq #0x3,D5" + dbf - four pages */
    for (page = 1; page <= 4; page++) {
        /* 0x00E17DE4-0x00E17E02: bytes of payload left from this page on,
         * at most 0x400 */
        remaining = (int32_t)rec->data_length - ((int32_t)(page - 1) << 10);
        if (remaining > 0x400) {
            remaining = 0x400;
        }

        /* 0x00E17E08: this page's address ("tst.l (0x38,A4)") */
        if (rec->data_pages[page - 1] == 0) {
            if ((int16_t)remaining <= 0) {
                return (int16_t)last;       /* 0x00E17E10 ble 0x00E17E7E */
            }
            return -1;                      /* 0x00E17E12 bra 0x00E17E46 */
        }
        if ((int16_t)remaining <= 0) {
            return (int16_t)last;           /* 0x00E17E16 ble 0x00E17E7E */
        }

        /* 0x00E17E18-0x00E17E28 */
        chunk = (int32_t)(int16_t)remaining;
        total += chunk;
        if ((int32_t)(int16_t)hdr->length + 1 < total) {
            return -1;                                          /* 0x00E17E28 */
        }

        /* 0x00E17E2A-0x00E17E44: always data_pages[0] ("move.l (0x3c,A2)") */
        NETBUF_$GETVA(rec->data_pages[0], &va, &status);
        if (status != status_$ok) {
            return -1;                      /* 0x00E17E44 falls into 0x00E17E46 */
        }

        /* 0x00E17E4A-0x00E17E66: (chunk + 1) / 2 words */
        words = (chunk + 1) / 2;
        last = XNS_IDP_$CHECKSUM((uint16_t *)ARCH_VA_TO_PTR(va), (int16_t)words);
        sum += last;

        NETBUF_$RTNVA(&va);                                     /* 0x00E17E6C */
    }

    return (int16_t)sum;                                        /* 0x00E17E7C */
}

/*
 * xns_$is_broadcast_addr - is this IDP address the broadcast host or ours?
 *
 * Re-emitted from the disassembly (source-tydd).  True (0xFF) when the host
 * part is all ones, or when the network is the network of one of the eight
 * ROUTE ports and the host is one of the registered local addresses.
 *
 * Register roles (0x00E17E88-0x00E17F00):
 *   A0  the address: network longword at +0, host words at +4/+6/+8
 *   D0b the result, "clr.b" then "st"
 *   A2  XNS_IDP_$DATA.ports[p] ("movea.l A5,A2", "lea (0xc,A2),A2"),
 *       D1 the dbf count (eight ports)
 *   A1  first the port's network record (its net_addr_ptr, 0x00E17EB0),
 *       then XNS_IDP_$DATA.addrs[i] ("lea (A5),A1", "addq.l #0x6,A1"),
 *       D2 the dbf count (registered_count: entries 0..count)
 *
 * The port's network is read through ports[p].net_addr_ptr, the VA
 * XNS_IDP_$INIT copied out of ROUTE_$PORTP (0x00E3031E), and the image
 * tests it for nothing before "move.l (A1),D2" - a port with a zero VA
 * reads the longword at 0.  The comparisons are word compares.
 *
 * @param addr      Pointer to 12-byte XNS address (network + host + socket)
 *
 * @return 0xFF (-1) if broadcast/local, 0 if remote
 *
 * Original address: 0x00E17E88
 */
int8_t xns_$is_broadcast_addr(void *addr)
{
    const uint16_t *w = (const uint16_t *)addr; /* A0, as the words it reads */
    uint32_t network = *(const uint32_t *)addr; /* (A0), 0x00E17EB6 */
    uint16_t host0 = w[2];                      /* (0x4,A0) */
    uint16_t host1 = w[3];                      /* (0x6,A0) */
    uint16_t host2 = w[4];                      /* (0x8,A0) */
    int16_t  p;                                 /* eight ports (D1 dbf) */
    int16_t  count;                             /* D2 */
    int16_t  i;

    /* 0x00E17E94-0x00E17EAA: "move.w #-0x1,D1w" against +8, +4, +6 */
    if (host2 == 0xFFFF && host0 == 0xFFFF && host1 == 0xFFFF) {
        return -1;                                              /* 0x00E17EE6 */
    }

    /* 0x00E17EAC-0x00E17EF4: "moveq #0x7,D1" + dbf over the port table */
    for (p = 0; p < XNS_MAX_PORTS; p++) {
        const uint32_t *port_network =
            (const uint32_t *)ARCH_VA_TO_PTR(XNS_IDP_$DATA.ports[p].net_addr_ptr);

        if (*port_network != network) {                         /* 0x00E17EB6 */
            continue;
        }
        count = XNS_IDP_$DATA.registered_count;                 /* 0x00E17EBA */
        if (count < 0) {                                        /* bmi */
            continue;
        }
        /* 0x00E17EC8-0x00E17EEC: dbf D2 - entries 0..count */
        for (i = 0; i <= count; i++) {
            if (XNS_IDP_$DATA.addrs[i][2] == host2 &&           /* (0x24,A1) */
                XNS_IDP_$DATA.addrs[i][1] == host1 &&           /* (0x22,A1) */
                XNS_IDP_$DATA.addrs[i][0] == host0) {           /* (0x20,A1) */
                return -1;                                      /* 0x00E17EE6 */
            }
        }
    }

    return 0;                                                   /* clr.b D0b */
}

/*
 * xns_$is_local_addr - Check if host portion is broadcast
 *
 * Validates that the host portion of an address is not the
 * broadcast address (all 0xFF bytes).
 *
 * @param addr      Pointer to host address (6 bytes)
 *
 * @return 0xFF (-1) if broadcast, 0 if OK
 *
 * Original address: 0x00E17850
 */
int8_t xns_$is_local_addr(void *addr)
{
    uint8_t *host = (uint8_t *)addr;

    /* Check if all bytes are 0xFF (broadcast) */
    if (host[0] == 0xFF && host[1] == 0xFF && host[2] == 0xFF &&
        host[3] == 0xFF && host[4] == 0xFF && host[5] == 0xFF) {
        return -1;  /* Broadcast */
    }

    return 0;  /* Not broadcast */
}
