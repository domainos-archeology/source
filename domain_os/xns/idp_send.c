/*
 * XNS IDP Send Operations
 *
 *   XNS_IDP_$OS_SEND:  0x00E18256   (594 bytes)
 *   XNS_IDP_$SEND:     0x00E18A66   (292 bytes)
 *
 * Both were re-emitted against the disassembly for source-tvrs; every basic
 * block of both functions is accounted for below and the addresses are cited
 * inline.
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$OS_SEND - Send a packet (OS-level)
 *
 * Builds the 0x4C-byte mac_os_$send_pkt_t the port driver wants out of the
 * caller's xns_$os_send_rec_t, optionally building the IDP header first,
 * resolves the next hop when the channel is not a connected one, and hands
 * the result to MAC_OS_$SEND.
 *
 * Frame (link.w A6,-0xac at 0x00E18256):
 *   A6-0x88  the mac_os_$send_pkt_t handed to MAC_OS_$SEND (0x4C bytes)
 *   A6-0x98  the FIM_$CLEANUP status
 *   A6-0xA0  RIP_$FIND_NEXTHOP's port_ret word
 *   A6-0x38  RIP_$FIND_NEXTHOP's rip_$nexthop_t, also MAC_OS_$ARP's input
 *   A6-0x28  the FIM cleanup record
 *   A6-0x10  the 12-byte destination copied out of the IDP header
 *
 * A5 is loaded absolutely with `lea (0xe2b314).l,A5' (0x00E1825E), so the
 * module base is the IDP state itself and the channel pointer A2 is
 * A5 + channel * 0x48 - i.e. &state->channels[channel] shifted down by the
 * 0xA0 array base, which is why the field displacements below read 0xA4/0xD4
 * rather than 0x04/0x34.
 *
 * Original address: 0x00E18256
 */
void XNS_IDP_$OS_SEND(int16_t *channel_ptr, xns_$os_send_rec_t *rec,
                      int16_t *len_sent_ret, status_$t *status_ret)
{
    xns_$idp_state_t   *state = XNS_$IDP_STATE;
    xns_$channel_t     *chan;
    xns_$idp_header_t  *hdr;
    boolean             is_connected;   /* D0b, `sne' at 0x00E182AA */
    boolean             build_header;   /* D1b, `sne' at 0x00E182B2 */
    int16_t             port;           /* D2w */
    int                 i;

    mac_os_$send_pkt_t  send_rec;       /* A6-0x88 */
    status_$t           fim_status;     /* A6-0x98 */
    int16_t             nexthop_port;   /* A6-0xA0 */
    rip_$nexthop_t      nexthop;        /* A6-0x38 */
    uint8_t             cleanup[24];    /* A6-0x28 */
    rip_$dest_addr_t    dest;           /* A6-0x10 */

    /* 0x00E18264-0x00E1826E */
    *len_sent_ret = 0;
    *status_ret = status_$ok;

    /* 0x00E18270-0x00E18286 */
    fim_status = FIM_$CLEANUP(cleanup);
    if (fim_status != status_$cleanup_handler_set) {
        /* 0x00E18498: the fault path returns the FIM status, no release */
        *status_ret = fim_status;
        return;
    }

    /* 0x00E1828A: D3 = rec->hdr_desc.address, kept for the whole function */
    hdr = (xns_$idp_header_t *)ARCH_VA_TO_PTR(rec->hdr_desc.address);

    /* 0x00E18292-0x00E182A0: A2 = A5 + channel * 0x48 */
    chan = &state->channels[*channel_ptr];

    /* 0x00E182A4-0x00E182B2: byte btst on the high half of the flags word */
    is_connected = (chan->flags & XNS_CHAN_FLAG_CONNECT) ? true : false;
    build_header = (chan->flags & XNS_CHAN_FLAG_BUILD_HEADER) ? true : false;

    /* 0x00E182B4: bpl -> 0x00E18342 */
    if (build_header < 0) {
        int32_t total;
        mac_os_$buf_desc_t *buf;

        hdr->checksum = 0xFFFF;                     /* 0x00E182BC */

        /* 0x00E182C4-0x00E182FC: walk the chain hanging off rec->hdr_desc,
         * accumulating lengths into D1 which starts at hdr_desc.length. */
        total = rec->hdr_desc.length;
        for (buf = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(rec->hdr_desc.next);
             buf != NULL;
             buf = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(buf->next)) {
            int32_t len = buf->length;              /* 0x00E182CE */

            /* 0x00E182D0-0x00E182D8: negative length, or a positive length
             * with a nil address, is rejected. */
            if (len < 0 || (len > 0 && buf->address == 0)) {
                FIM_$RLS_CLEANUP(cleanup);
                *status_ret = status_$xns_illegal_buffer_spec;   /* 0x3B0008 */
                return;
            }
            total += len;                           /* 0x00E182F2 */
        }

        /* 0x00E18302-0x00E18306: `add.w (0x36,A3),D1w' adds the LOW WORD of
         * the longword at rec +0x34 and only the low word of the sum is
         * stored. */
        hdr->length = (uint16_t)((uint16_t)total + (uint16_t)rec->data_length);

        hdr->transport_ctl = 0;                     /* 0x00E1830A */
        /* 0x00E1830E: `move.b (0x2d,A3)' - the low byte of the +0x2C word */
        hdr->packet_type = (uint8_t)rec->packet_type;

        /* 0x00E18314: bpl -> 0x00E18336 */
        if (is_connected < 0) {
            /* 0x00E18318-0x00E18332: two three-longword copies, the channel's
             * connected destination then its bound source. */
            hdr->dest_network = chan->dest_network;
            for (i = 0; i < 6; i++) {
                hdr->dest_host[i] = chan->dest_host[i];
            }
            hdr->dest_socket = chan->dest_socket;
            hdr->src_network = chan->src_network;
            for (i = 0; i < 6; i++) {
                hdr->src_host[i] = chan->src_host[i];
            }
            hdr->src_socket = chan->src_port;
        } else {
            /* 0x00E18336-0x00E1833E: 24 bytes straight out of the record. */
            hdr->dest_network = rec->dest_addr.network;
            for (i = 0; i < 6; i++) {
                hdr->dest_host[i] = rec->dest_addr.host[i];
            }
            hdr->dest_socket = rec->dest_addr.socket;
            hdr->src_network = rec->src_addr.network;
            for (i = 0; i < 6; i++) {
                hdr->src_host[i] = rec->src_addr.host[i];
            }
            hdr->src_socket = rec->src_addr.socket;
        }
    }

    /* 0x00E18342: bpl -> 0x00E1835E */
    if (is_connected < 0) {
        /* 0x00E18346: the port was fixed when the channel was connected */
        port = chan->connected_port;

        /* 0x00E1834A-0x00E18358: six longwords, chan +0xBC -> record +0x00.
         * Note that this covers only the record's first 24 bytes, so
         * is_broadcast (+0x18) is left as it was - the original does not
         * initialise it on this path. */
        for (i = 0; i < 0x18; i++) {
            ((uint8_t *)&send_rec)[i] = chan->mac_info[i];
        }
        /* 0x00E1835A: bra -> 0x00E183E0 */
    } else {
        /* 0x00E18360-0x00E1836C: three longwords, header +0x06 -> A6-0x10 */
        dest.network = hdr->dest_network;
        dest.host_hi = (uint16_t)((hdr->dest_host[0] << 8) | hdr->dest_host[1]);
        dest.host_lo = ((uint32_t)hdr->dest_host[2] << 24) |
                       ((uint32_t)hdr->dest_host[3] << 16) |
                       ((uint32_t)hdr->dest_host[4] << 8) |
                       (uint32_t)hdr->dest_host[5];
        dest.socket = hdr->dest_socket;

        /* 0x00E1836E-0x00E18388: the second argument is `st -(SP)', a Pascal
         * boolean true pushed by value.  The function result slot is reserved
         * and then discarded with the arguments by `lea (0x14,SP),SP'. */
        (void)RIP_$FIND_NEXTHOP(&dest, true, &nexthop_port, &nexthop, status_ret);

        port = nexthop_port;                        /* 0x00E1838C */
        if (*status_ret != status_$ok) {            /* 0x00E18394 */
            goto release;
        }

        /* 0x00E1839E */
        if (port == -1) {
            FIM_$RLS_CLEANUP(cleanup);
            *status_ret = status_$xns_network_unreachable;   /* 0x3B0013 */
            return;
        }

        /* 0x00E183BC-0x00E183D2: ARP fills the record's link address (+0x00)
         * and its broadcast flag (+0x18).  Its function result is discarded
         * the same way. */
        MAC_OS_$ARP(&nexthop, port, (uint16_t *)&send_rec.link_addr,
                    (uint8_t *)&send_rec.is_broadcast, status_ret);
        if (*status_ret != status_$ok) {            /* 0x00E183DA */
            goto release;
        }
    }

    /* 0x00E183E0-0x00E183F0: the channel number is re-read from the argument */
    xns_$add_port((uint16_t)*channel_ptr, port, status_ret);
    if (*status_ret != status_$ok) {                /* 0x00E183F6 */
        goto release;
    }

    send_rec.frame_type = XNS_MAC_FRAME_TYPE;       /* 0x00E183FC */

    /* 0x00E18408-0x00E18414: three longwords, rec +0x18 -> record +0x1C */
    send_rec.hdr_desc = rec->hdr_desc;

    /* 0x00E18416 */
    send_rec.hdr_prebuilt = rec->hdr_prebuilt;

    /* 0x00E1841C-0x00E18428: five longwords, rec +0x34 -> record +0x38 */
    send_rec.data_length = rec->data_length;
    for (i = 0; i < 4; i++) {
        send_rec.data_pages[i] = rec->data_pages[i];
    }

    /* 0x00E1842E: `cmpi.w #-1,(A3) / beq' - the checksum is computed only
     * when the header does NOT already say "no checksum". */
    if (hdr->checksum != 0xFFFF) {
        int16_t csum = xns_$get_checksum(&send_rec);    /* 0x00E18438 */

        hdr->checksum = (uint16_t)csum;                 /* 0x00E1843E */
        if (csum == -1) {                               /* 0x00E18440 */
            FIM_$RLS_CLEANUP(cleanup);
            *status_ret = status_$xns_bad_checksum;     /* 0x3B0011 */
            return;
        }
    }

    /* 0x00E1845C-0x00E1847E: the channel argument is
     * `pea (0x48,A5,D1*0x1)' with D1 = port * 12, i.e. state +0x40 +
     * port * 0x0C + 0x08 - the port's MAC socket word. */
    MAC_OS_$SEND((int16_t *)&state->ports[port].mac_socket, &send_rec,
                 len_sent_ret, status_ret);

    /* 0x00E18486-0x00E1848A */
    if (*status_ret == status_$ok) {
        state->packets_sent += 1;
    }

release:
    /* 0x00E1848C */
    FIM_$RLS_CLEANUP(cleanup);
}

/*
 * XNS_IDP_$SEND - Send a packet (user-level)
 *
 * Validates the channel and its owner, copies the user's request into a
 * kernel-resident xns_$os_send_rec_t, clears the flag byte of every buffer in
 * the user's chain, and calls XNS_IDP_$OS_SEND.
 *
 * Frame (link.w A6,-0x74 at 0x00E18A66):
 *   A6-0x48  the xns_$os_send_rec_t built for XNS_IDP_$OS_SEND
 *   A6-0x60  the FIM cleanup record
 *   A6-0x68  the status XNS_IDP_$OS_SEND returned
 *   A6-0x6C  the FIM_$CLEANUP status
 *   A6-0x6E  the length word XNS_IDP_$OS_SEND returned
 *
 * Original address: 0x00E18A66
 */
void XNS_IDP_$SEND(uint16_t *channel_ptr, xns_$idp_send_t *send_params,
                   int16_t *len_sent_ret, status_$t *status_ret)
{
    xns_$idp_state_t   *state = XNS_$IDP_STATE;
    xns_$channel_t     *chan;
    xns_$idp_iov_t     *iov;

    xns_$os_send_rec_t  rec;            /* A6-0x48 */
    uint8_t             cleanup[24];    /* A6-0x60 */
    status_$t           send_status;    /* A6-0x68 */
    status_$t           fim_status;     /* A6-0x6C */
    int16_t             len_sent;       /* A6-0x6E */

    /* 0x00E18A74-0x00E18A7E */
    *len_sent_ret = 0;
    *status_ret = status_$ok;

    /* 0x00E18A84: `cmpi.w #0x10,(A2) / bcc' - an UNSIGNED bound */
    if (*channel_ptr >= XNS_MAX_CHANNELS) {
        *status_ret = status_$xns_bad_channel;      /* 0x3B0004 */
        return;
    }

    /* 0x00E18A8A-0x00E18A94 */
    chan = &state->channels[*channel_ptr];

    /* 0x00E18A98: `tst.w (0xe4,A0) / bpl' - the state word's sign bit */
    if (chan->state >= 0) {
        *status_ret = status_$xns_bad_channel;
        return;
    }

    /* 0x00E18A9E-0x00E18AAE: the AS_ID lives in word bits 5..10 and is
     * compared against the word at 0x00E2060A. */
    if (((chan->flags & XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT)
        != PROC1_$AS_ID) {
        *status_ret = status_$xns_bad_channel;
        return;
    }

    /* 0x00E18ABA-0x00E18AD0 */
    fim_status = FIM_$CLEANUP(cleanup);
    if (fim_status != status_$cleanup_handler_set) {
        /* 0x00E18B7A */
        *status_ret = fim_status;
        return;
    }

    /* 0x00E18AD8-0x00E18AE6: a nil header address, or a header shorter than
     * the 30-byte IDP header, is rejected. */
    if (send_params->hdr_desc.address == 0 ||
        send_params->hdr_desc.length < XNS_IDP_HEADER_SIZE) {
        FIM_$RLS_CLEANUP(cleanup);
        *status_ret = status_$xns_illegal_buffer_spec;    /* 0x3B0008 */
        return;
    }

    /* 0x00E18B00-0x00E18B08: 24 bytes, user record +0x00 -> local +0x00 */
    rec.dest_addr = send_params->dest_addr;
    rec.src_addr = send_params->src_addr;

    /* 0x00E18B10: a WORD copy of the packet type */
    rec.packet_type = send_params->packet_type;

    /* 0x00E18B16-0x00E18B1A: the payload length and the first payload page.
     * The remaining three pages (+0x3C..+0x47) are left uninitialised, as in
     * the original; XNS_IDP_$OS_SEND copies them anyway but data_length is
     * zero so no driver looks at them. */
    rec.data_length = 0;
    rec.data_pages[0] = 0;

    /* 0x00E18B1E-0x00E18B2A: three longwords, user +0x18 -> local +0x18 */
    rec.hdr_desc = send_params->hdr_desc;

    /* 0x00E18B2C: the kernel builds the buffers itself */
    rec.hdr_prebuilt = false;

    /* 0x00E18B30-0x00E18B42: clear the flag byte of every user buffer */
    for (iov = (xns_$idp_iov_t *)ARCH_VA_TO_PTR(send_params->hdr_desc.next);
         iov != NULL;
         iov = (xns_$idp_iov_t *)ARCH_VA_TO_PTR(iov->desc.next)) {
        iov->flags = 0;
    }

    /* 0x00E18B44-0x00E18B58: note that the CALLER's channel pointer is
     * forwarded, not a copy. */
    XNS_IDP_$OS_SEND((int16_t *)channel_ptr, &rec, &len_sent, &send_status);

    /* 0x00E18B5C-0x00E18B6C */
    *len_sent_ret = len_sent;
    *status_ret = send_status;

    /* 0x00E18B6E */
    FIM_$RLS_CLEANUP(cleanup);
}
