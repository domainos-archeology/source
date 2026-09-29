/*
 * XNS IDP Receive
 *
 * XNS_IDP_$RECEIVE (0x00E18CE2) and its nested procedure
 * xns_$copy_packet_data (0x00E18C5E).
 *
 * Original address: 0x00E18CE2
 *
 * Module data through XNS_IDP_$DATA / XNS_ERROR_$DATA: Claude Opus 5.5
 * (source-iq58).
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$RECEIVE's frame, as far as its nested procedure reaches into it.
 *
 * xns_$copy_packet_data (0x00E18C5E) is a Pascal nested procedure: it takes
 * two arguments of its own but reads the parent through the static link
 * ("movea.l (A6),A2" at 0x00E18C6A, so A2 is XNS_IDP_$RECEIVE's frame
 * pointer).  The only two slots it touches are:
 *
 *   A6-0x5C  the current buffer descriptor (0x00E18C80, 0x00E18C94,
 *            0x00E18CC4)
 *   A6-0x76  how many bytes of that descriptor are already filled
 *            (0x00E18C7A, 0x00E18C8E, 0x00E18CBA, 0x00E18CC0)
 *
 * Both are carried explicitly here.  The descriptor is a real pointer, not a
 * target VA: the parent seeds it with "lea (0x18,A3),A2" (0x00E18DCE), the
 * address of a field of its own second argument.  Only the LINKS between
 * descriptors are VAs.
 */
typedef struct xns_$idp_receive_frame_t {
    mac_os_$buf_desc_t *iov;        /* A6-0x5C */
    int16_t             partial;    /* A6-0x76 */
} xns_$idp_receive_frame_t;

/*
 * xns_$copy_packet_data (0x00E18C5E) - nested procedure of XNS_IDP_$RECEIVE
 *
 * Copies `length' bytes starting at the VA held in *src_cell into the
 * caller's buffer chain, picking up where the previous call left off.  A
 * descriptor is only advanced past when it has been filled: the last partly
 * used one stays current and `partial' says how far into it the next copy
 * starts.
 *
 * @param parent    the two parent-frame slots (see above)
 * @param src_cell  A6+0x08: the ADDRESS of a longword holding the source VA
 *                  ("movea.l (0x8,A6),A0 / move.l (A0),D3" 0x00E18C6C)
 * @param length    A6+0x0C: a word ("move.w (0xc,A6),D0w" 0x00E18C66)
 */
static void xns_$copy_packet_data(xns_$idp_receive_frame_t *parent,
                                  const uint32_t *src_cell, int16_t length)
{
    uint32_t src_va;        /* D3 */
    int16_t  remaining;     /* D4 */
    int32_t  chunk;         /* D5 */
    int32_t  copied;        /* D2 */

    src_va    = *src_cell;                          /* 0x00E18C70 */
    remaining = length;                             /* 0x00E18C72 */

    /*
     * 0x00E18C74 "bra.b 0x00E18CD0" enters at the loop test, which is the
     * pair 0x00E18CD0 "beq" (nothing left to copy) and 0x00E18CD2
     * "tst.l (-0x5c,A2) / bne" (a descriptor is still available).
     */
    while (remaining != 0 && parent->iov != NULL) {
        mac_os_$buf_desc_t *iov = parent->iov;
        uint32_t dst_va;

        /* 0x00E18C76-0x00E18C8C: clamp to what is left of this descriptor. */
        chunk = remaining;
        if (chunk > iov->length - (int32_t)parent->partial) {
            chunk = iov->length - (int32_t)parent->partial;
        }

        /* 0x00E18C8E-0x00E18C9C: dst = descriptor address + partial. */
        dst_va = (uint32_t)((int32_t)parent->partial + (int32_t)iov->address);

        copied = chunk;                             /* 0x00E18CA0 ext.l D2 */
        OS_$DATA_COPY(ARCH_VA_TO_PTR(src_va), ARCH_VA_TO_PTR(dst_va),
                      (uint32_t)copied);            /* 0x00E18CAA */

        src_va += (uint32_t)copied;                 /* 0x00E18CB4 */
        remaining = (int16_t)(remaining - (int16_t)chunk);  /* 0x00E18CB6 */

        if (remaining == 0) {
            /* 0x00E18CBA: this descriptor stays current, partly filled. */
            parent->partial = (int16_t)(parent->partial + (int16_t)chunk);
        } else {
            /* 0x00E18CC0-0x00E18CCC: it is full, move to the next. */
            parent->partial = 0;
            parent->iov = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(iov->next);
        }
    }
}

/*
 * XNS_IDP_$RECEIVE - receive a packet (user-level), 0x00E18CE2
 *
 * @param channel_ptr   the channel index, read as a word
 * @param recv_params   xns_$idp_recv_t: the caller's buffer chain going in,
 *                      the packet's addresses and type coming back
 * @param status_ret    Output: status code
 */
void XNS_IDP_$RECEIVE(uint16_t *channel_ptr, xns_$idp_recv_t *recv_params,
                      status_$t *status_ret)
{
    xns_$channel_t *chan;               /* A2 */
    uint16_t  channel;
    sock_$pkt_info_t rec;               /* A6-0x40, what SOCK_$GET fills in */
    uint32_t  data_va;                  /* A6-0x70 */
    status_$t cleanup_status;           /* A6-0x74 */
    uint8_t   cleanup_buf[24];          /* A6-0x58 */
    xns_$idp_receive_frame_t frame;     /* A6-0x5C and A6-0x76 */
    mac_os_$buf_desc_t *head;
    int32_t   capacity;                 /* D0 */
    int i;

    channel = *channel_ptr;                             /* 0x00E18CF0 */
    *status_ret = status_$ok;                           /* 0x00E18CF8 */
    data_va = 0;                                        /* 0x00E18CFA */

    /* 0x00E18CFE "cmpi.w #0x10,(A0)" / `bcc' - an UNSIGNED compare. */
    if (channel >= XNS_MAX_CHANNELS) {
        *status_ret = status_$xns_bad_channel;
        return;
    }

    /* 0x00E18D04-0x00E18D0E: the channel base is formed with WORD
     * arithmetic; with channel < 16 (checked above) that is simply
     * A5 + channel * 0x48. */
    chan = &XNS_IDP_$DATA.channels[channel];

    if (chan->state >= 0) {                             /* 0x00E18D12 `bpl' */
        *status_ret = status_$xns_bad_channel;
        return;
    }

    /*
     * 0x00E18D18 "tst.b (0xda,A2)" / `bmi' tests bit 15 of the flags word -
     * an OS channel opened with XNS_OPEN_FLAG_NO_ALLOC - and skips the
     * ownership check for it.  Written as a word mask so the host build
     * looks at the same bit.
     */
    if ((chan->flags & 0x8000u) == 0) {
        uint16_t chan_as_id =
            (uint16_t)((chan->flags &
                        XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT);

        if (chan_as_id != PROC1_$AS_ID) {               /* 0x00E18D28 */
            *status_ret = status_$xns_bad_channel;
            return;
        }
    }

    if (chan->user_socket == XNS_NO_SOCKET) {
        *status_ret = status_$xns_no_socket;            /* 0x00E18D42 */
        return;
    }

    /* 0x00E18D4C: a word result slot, then &rec and the socket number. */
    if (SOCK_$GET(chan->user_socket, &rec) >= 0) {
        *status_ret = status_$xns_no_data;              /* 0x00E18D66 */
        return;
    }

    /*
     * 0x00E18D70 "btst.b #0x3,(0xda,A2)" is bit 3 of the flags word's HIGH
     * byte, i.e. word bit 11 - the channel builds its own IDP header, so the
     * caller wants the received one's addresses handed back.
     */
    if (chan->flags & XNS_CHAN_FLAG_BUILD_HEADER) {
        const uint8_t *hdr = (const uint8_t *)ARCH_VA_TO_PTR(rec.hdr);
        uint8_t *dst = (uint8_t *)recv_params;

        /* 0x00E18D7C-0x00E18D8A: 24 bytes from the header's +0x06. */
        for (i = 0; i < 24; i++) {
            dst[i] = hdr[offsetof(xns_$idp_header_t, dest_network) + i];
        }
        /* 0x00E18D8E-0x00E18D96: the packet type, zero-extended. */
        recv_params->packet_type =
            (uint16_t)hdr[offsetof(xns_$idp_header_t, packet_type)];
    }

    recv_params->mac_src_hi = rec.src_addr;             /* 0x00E18D9E */
    recv_params->mac_src_lo = rec.src_port;             /* 0x00E18DA4 */

    cleanup_status = FIM_$CLEANUP(cleanup_buf);         /* 0x00E18DAE */
    if (cleanup_status != status_$cleanup_handler_set) {
        /* 0x00E18EE4-0x00E18F00: give the packet back and report. */
        NETBUF_$RTN_PKT(&rec.hdr, &data_va, rec.data_pages,
                        (int16_t)rec.data_len);
        *status_ret = cleanup_status;
        return;
    }

    if (recv_params->iov.address == 0) {                /* 0x00E18DC8 */
        *status_ret = status_$xns_illegal_buffer_spec;
        goto cleanup;
    }

    /*
     * 0x00E18DCE "lea (0x18,A3),A2": the chain head is the descriptor
     * embedded in the caller's record.  Formed off the record base rather
     * than as "&recv_params->iov" because the record is packed.
     */
    head = (mac_os_$buf_desc_t *)((uint8_t *)recv_params +
                                  offsetof(xns_$idp_recv_t, iov));

    /* 0x00E18DD2-0x00E18E0C: total the chain's capacity, rejecting a
     * negative length or a positive length with no address. */
    frame.iov = head;
    capacity = 0;
    while (frame.iov != NULL) {
        int32_t len = frame.iov->length;                /* 0x00E18DDE */

        if (len < 0) {                                  /* 0x00E18DE0 `bmi' */
            *status_ret = status_$xns_illegal_buffer_spec;
            goto cleanup;
        }
        if (len > 0 && frame.iov->address == 0) {       /* 0x00E18DE4/0x00E18DE8 */
            *status_ret = status_$xns_illegal_buffer_spec;
            goto cleanup;
        }
        capacity += frame.iov->length;                  /* 0x00E18DFC */
        frame.iov = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(frame.iov->next);
    }

    /* 0x00E18E0E-0x00E18E1E: both lengths are zero-extended words. */
    if ((int32_t)((uint32_t)rec.hdr_len + (uint32_t)rec.data_len) > capacity) {
        *status_ret = status_$xns_buffer_too_small;
        goto cleanup;
    }

    if (rec.data_len != 0) {                            /* 0x00E18E2E */
        /* 0x00E18E3C: the first payload page is pushed BY VALUE. */
        NETBUF_$GETVA(rec.data_pages[0], &data_va, status_ret);
        if (*status_ret != status_$ok) {                /* 0x00E18E4E */
            data_va = 0;                                /* 0x00E18E52 */
            goto cleanup;
        }
    }

    frame.iov = head;                                   /* 0x00E18E58 */
    frame.partial = 0;                                  /* 0x00E18E5C */

    if (rec.hdr_len != 0) {                             /* 0x00E18E60 */
        /* 0x00E18E6C "pea (-0x40,A6)": the record's own header VA cell. */
        xns_$copy_packet_data(&frame, &rec.hdr, (int16_t)rec.hdr_len);
    }
    if (rec.data_len != 0) {                            /* 0x00E18E76 */
        xns_$copy_packet_data(&frame, &data_va, (int16_t)rec.data_len);
    }

    /*
     * 0x00E18E8C-0x00E18EB4.  The descriptor the copy stopped inside is
     * shortened to the number of bytes actually put in it, and every
     * descriptor after it is emptied, so the caller can walk the chain and
     * see exactly how much arrived.
     *
     * Note that 0x00E18E98 writes through the cursor without testing it: a
     * positive `partial' can only be left behind while a descriptor is still
     * current, so the image never checks.
     */
    if (frame.partial > 0) {                            /* 0x00E18E8C `ble' */
        frame.iov->length = (int32_t)frame.partial;     /* 0x00E18E9C */
        frame.iov = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(frame.iov->next);
    }
    while (frame.iov != NULL) {                         /* 0x00E18EB0 */
        frame.iov->length = 0;                          /* 0x00E18EA4 */
        frame.iov = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(frame.iov->next);
    }

    *status_ret = status_$ok;                           /* 0x00E18EBA */

cleanup:
    /* 0x00E18EBC-0x00E18EE2 */
    NETBUF_$RTN_PKT(&rec.hdr, &data_va, rec.data_pages, (int16_t)rec.data_len);
    FIM_$RLS_CLEANUP(cleanup_buf);
}
