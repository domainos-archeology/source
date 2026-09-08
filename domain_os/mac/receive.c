/*
 * MAC_$RECEIVE - take the next packet off a MAC channel's socket
 *
 * Validates the channel and its owner, pops one packet with SOCK_$GET, copies
 * the link address and arrival time into the caller's descriptor, and then
 * scatters the header and payload into the caller's buffer chain.
 *
 * Original address: 0x00E0BDB0, size 558 bytes (0x00E0BDB0-0x00E0BFDD).
 * A5 = 0x00E22990 (MAC_OS_$DATA).
 *
 * No hardware is touched, so the body is portable (bead source-1irc; it used
 * to sit under "#if defined(ARCH_M68K)").
 */

#include "mac/mac_internal.h"

/*
 * mac_$copy_to_buffers (0x00E0BD2C, 132 bytes) - the nested Pascal procedure
 * that pours `length` bytes from one source VA into the caller's buffer chain.
 *
 * It takes two stack arguments plus a static link:
 *   (0x08,A6)  a cell holding the SOURCE VA.  The value is READ ONCE into D3
 *              ("move.l (A0),D3" at 0x00E0BD3E) and advanced only in D3; the
 *              cell itself is never written back, so the caller's
 *              sock_$pkt_info_t.hdr keeps pointing at the start of the header
 *              for the NETBUF_$RTN_PKT that follows (bead source-1irc).
 *   (0x0C,A6)  the word length
 *   (A6)       the static link, through which it reads and writes the parent's
 *              A6-0x5c (current chain entry) and A6-0x72 (offset within it).
 * Those two uplevel slots are passed explicitly here.
 */
static void mac_$copy_to_buffers(const uint32_t *src_va_ptr, int16_t length,
                                 mac_os_$buf_desc_t **cur_buf,
                                 int16_t *buf_offset)
{
    uint32_t    src = *src_va_ptr;      /* D3 */
    int16_t     remaining = length;     /* D4 */

    /* 0x00E0BD9C: the loop runs while remaining != 0 AND there is a buffer */
    while (remaining != 0 && *cur_buf != NULL) {
        int32_t chunk;          /* D5 */
        int32_t buf_remaining;  /* D1 */
        uint32_t dest;

        /* 0x00E0BD44-0x00E0BD5A: chunk = min(remaining, entry->length - off) */
        chunk = (int32_t)remaining;
        buf_remaining = (*cur_buf)->length - (int32_t)*buf_offset;
        if (chunk > buf_remaining) {
            chunk = buf_remaining;
        }

        /* 0x00E0BD5C-0x00E0BD6A: dest = entry->address + off */
        dest = (uint32_t)((int32_t)*buf_offset + (int32_t)(*cur_buf)->address);

        /* 0x00E0BD6E-0x00E0BD7E: OS_$DATA_COPY(src, dest, chunk) */
        OS_$DATA_COPY((char *)ARCH_VA_TO_PTR(src),
                      (char *)ARCH_VA_TO_PTR(dest),
                      (int)chunk);

        /* 0x00E0BD82: the SOURCE advances by the same count, in D3 only */
        src += (uint32_t)chunk;

        /* 0x00E0BD84-0x00E0BD9A */
        remaining = (int16_t)(remaining - (int16_t)chunk);
        if (remaining == 0) {
            *buf_offset = (int16_t)(*buf_offset + (int16_t)chunk);
        } else {
            *buf_offset = 0;
            *cur_buf = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR((*cur_buf)->next);
        }
    }
}

/*
 * MAC_$RECEIVE
 *
 * Parameters (0x08, 0x0C, 0x10 off A6):
 *   channel    - pointer to the channel number
 *   pkt_desc   - the caller's receive descriptor
 *   status_ret - status return
 */
void MAC_$RECEIVE(uint16_t *channel, mac_$recv_pkt_t *pkt_desc,
                  status_$t *status_ret)
{
    uint16_t            chan_num;
    mac_os_$channel_t  *chan;
    uint16_t            flags;
    status_$t           cleanup_status;
    uint8_t             cleanup_buf[24];    /* A6-0x58 */

    /*
     * A6-0x40: the record SOCK_$GET fills in.  Every field this routine
     * touches lands on a sock_$pkt_info_t offset: hdr 0x00 (-0x40),
     * src_addr 0x04 (-0x3c), src_port 0x08 (-0x38), dst_addr 0x0c (-0x34),
     * flags 0x10 (the byte at -0x2f), n_hops 0x12 (-0x2e), hops 0x14 (-0x2c),
     * data_len 0x2a (-0x16), hdr_len 0x2c (-0x14) and data_pages 0x30 (-0x10).
     */
    sock_$pkt_info_t    pkt_info;
    uint32_t            data_va;        /* A6-0x6c */
    uint16_t            header_len;
    uint16_t            data_len;
    int32_t             total_buf_size;
    mac_os_$buf_desc_t *chain_head;     /* A2 */
    mac_os_$buf_desc_t *cur_buf;        /* A6-0x5c */
    int16_t             buf_offset;     /* A6-0x72 */
    uint16_t            i;

    /* 0x00E0BDC6 / 0x00E0BDC8 */
    *status_ret = status_$ok;
    data_va = 0;

    /* 0x00E0BDCC: an unsigned compare, so a channel >= 10 is rejected */
    chan_num = *channel;
    if (chan_num >= MAC_MAX_CHANNELS) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /* 0x00E0BDD2-0x00E0BDE0 */
    chan  = &MAC_OS_$CHANNEL_TABLE[chan_num];
    flags = chan->flags;

    /* 0x00E0BDE4: btst.l #0x9,D1 on the whole word */
    if ((flags & MAC_OS_CHANNEL_IN_USE) == 0) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * 0x00E0BDEA-0x00E0BE00.  A promiscuous channel skips the owner test;
     * otherwise the owner recorded in the flags word's top six bits must be
     * this address space.
     *   move.w #0xfc,D2w / and.b (0x7b2,A2),D2b / lsr.w #0x2,D2w
     * works on the flags word's HIGH byte, i.e. word bits 10..15.
     */
    if ((flags & MAC_OS_CHANNEL_PROMISCUOUS) == 0) {
        uint16_t owner_asid =
            (uint16_t)((flags & MAC_OS_CHANNEL_OWNER_MASK) >> MAC_OS_CHANNEL_OWNER_SHIFT);

        if (owner_asid != (uint16_t)PROC1_$AS_ID) {
            *status_ret = status_$mac_channel_not_open;
            return;
        }
    }

    /* 0x00E0BE0C-0x00E0BE1A */
    if (chan->socket == MAC_NO_SOCKET) {
        *status_ret = status_$mac_no_socket_allocated;
        return;
    }

    /*
     * 0x00E0BE1E-0x00E0BE3E: SOCK_$GET(chan->socket, &pkt_info) with a
     * discarded word result; a non-negative answer means the queue was empty.
     */
    if (SOCK_$GET(chan->socket, &pkt_info) >= 0) {
        *status_ret = status_$mac_no_packet_available_to_receive;
        return;
    }

    /* 0x00E0BE42-0x00E0BE4E: btst.b #0,(-0x2f,A6) / sne - flags word bit 0 */
    pkt_desc->is_local = (pkt_info.flags & SOCK_PKT_FLAG_LOCAL) ? (int8_t)-1 : 0;

    /*
     * 0x00E0BE52-0x00E0BE6C: the hop count and that many words, copied
     * forward into the descriptor's link address.  Neither loop bounds the
     * count.
     */
    pkt_desc->link_addr.n_words = pkt_info.n_hops;
    for (i = 0; i < pkt_info.n_hops; i++) {
        pkt_desc->link_addr.addr[i] = pkt_info.hops[i];
    }

    /* 0x00E0BE6E-0x00E0BE7A */
    pkt_desc->time_high  = pkt_info.src_addr;
    pkt_desc->time_low   = pkt_info.src_port;
    pkt_desc->frame_type = pkt_info.dst_addr;

    /* 0x00E0BE80-0x00E0BE96 */
    cleanup_status = FIM_$CLEANUP(cleanup_buf);
    if (cleanup_status != status_$cleanup_handler_set) {
        /*
         * 0x00E0BFB4-0x00E0BFD2: the fault unwind.  It returns the buffers and
         * stores the fault status, but does NOT release the cleanup handler
         * and never pops the call's arguments (unlk restores SP).
         */
        NETBUF_$RTN_PKT(&pkt_info.hdr, &data_va,
                        pkt_info.data_pages, (int16_t)pkt_info.data_len);
        *status_ret = cleanup_status;
        return;
    }

    /*
     * 0x00E0BE9A-0x00E0BEDC: walk the caller's chain, summing the lengths as a
     * LONGWORD this time.  A negative length, or a positive length with a null
     * address, is rejected.
     */
    /*
     * 0x00E0BE9E: lea (0x1c,A3),A2.  Reached by displacement rather than by
     * &pkt_desc->buffers because mac_$recv_pkt_t is packed (its time_high sits
     * on an odd word boundary) and taking a member's address would advertise
     * byte alignment.
     */
    chain_head     = (mac_os_$buf_desc_t *)
                     ((uint8_t *)pkt_desc + offsetof(mac_$recv_pkt_t, buffers));
    cur_buf        = chain_head;
    total_buf_size = 0;

    while (cur_buf != NULL) {
        if (cur_buf->length < 0) {
            *status_ret = status_$mac_illegal_buffer_spec;
            goto cleanup_and_return;
        }
        if (cur_buf->length > 0 && cur_buf->address == 0) {
            *status_ret = status_$mac_illegal_buffer_spec;
            goto cleanup_and_return;
        }

        total_buf_size += cur_buf->length;
        cur_buf = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(cur_buf->next);
    }

    /*
     * 0x00E0BEDE-0x00E0BEFC: both lengths are ZERO-extended into longwords
     * before the sum is compared with the chain's capacity.
     */
    header_len = pkt_info.hdr_len;      /* record + 0x2C */
    data_len   = pkt_info.data_len;     /* record + 0x2A */

    if ((int32_t)((uint32_t)header_len + (uint32_t)data_len) > total_buf_size) {
        *status_ret = status_$mac_received_packet_too_big;
        goto cleanup_and_return;
    }

    /*
     * 0x00E0BEFE-0x00E0BF26: map the payload page in.  On failure the VA cell
     * is cleared so the NETBUF_$RTN_PKT below does not unmap anything.
     */
    if (data_len != 0) {
        NETBUF_$GETVA(pkt_info.data_pages[0], &data_va, status_ret);
        if (*status_ret != status_$ok) {
            data_va = 0;
            goto cleanup_and_return;
        }
    }

    /* 0x00E0BF28-0x00E0BF2C */
    cur_buf    = chain_head;
    buf_offset = 0;

    /* 0x00E0BF30-0x00E0BF44 */
    if (header_len != 0) {
        mac_$copy_to_buffers(&pkt_info.hdr, (int16_t)header_len,
                             &cur_buf, &buf_offset);
    }

    /* 0x00E0BF46-0x00E0BF5A */
    if (data_len != 0) {
        mac_$copy_to_buffers(&data_va, (int16_t)data_len,
                             &cur_buf, &buf_offset);
    }

    /*
     * 0x00E0BF5C-0x00E0BF84.  If the last buffer was only partly filled its
     * length is CUT DOWN to the number of bytes actually written and that
     * entry is stepped over; every remaining entry is then zeroed.
     *   tst.w (-0x72,A6) / ble -> the while test
     *   move.w (-0x72,A6),D2w / ext.l D2 / move.l D2,(A0)   entry->length
     *   bra -> advance
     */
    if (buf_offset > 0) {
        cur_buf->length = (int32_t)buf_offset;
        cur_buf = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(cur_buf->next);
    }
    while (cur_buf != NULL) {
        cur_buf->length = 0;
        cur_buf = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(cur_buf->next);
    }

    /* 0x00E0BF86 */
    *status_ret = status_$ok;

cleanup_and_return:
    /* 0x00E0BF8C-0x00E0BFA6 */
    NETBUF_$RTN_PKT(&pkt_info.hdr, &data_va,
                    pkt_info.data_pages, (int16_t)pkt_info.data_len);

    /* 0x00E0BFA8 */
    FIM_$RLS_CLEANUP(cleanup_buf);
}
