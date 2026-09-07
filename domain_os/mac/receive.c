/*
 * MAC_$RECEIVE - Receive a packet from a MAC channel
 *
 * Receives the next packet from the channel's socket queue.
 * Copies packet data into the provided buffer chain.
 *
 * Original address: 0x00E0BDB0
 * Original size: 558 bytes
 */

#include "mac/mac_internal.h"

/*
 * mac_$copy_to_buffers - Copy packet data to user buffers
 *
 * This is a nested procedure (Pascal-style) that accesses the parent's
 * stack frame for buffer chain state. We implement it as a static helper.
 *
 * Original address: 0x00E0BD2C
 * Original size: 132 bytes
 */
static void copy_to_buffers(
    uint32_t *src_va_ptr, /* Cell holding the source target VA (updated) */
    int16_t length,      /* Number of bytes to copy */
    mac_$buffer_t **cur_buf,  /* Pointer to current buffer (updated) */
    int16_t *buf_offset  /* Pointer to offset in current buffer (updated) */
)
{
    uint8_t *src = (uint8_t *)ARCH_VA_TO_PTR(*src_va_ptr);
    int16_t remaining = length;
    int16_t chunk_size;
    int32_t buf_remaining;

    while (remaining > 0 && *cur_buf != NULL) {
        /* Calculate how much space is left in current buffer */
        buf_remaining = (*cur_buf)->size - *buf_offset;

        /* Copy the smaller of: remaining data or buffer space */
        chunk_size = (remaining < buf_remaining) ? remaining : (int16_t)buf_remaining;

        /* Copy data to buffer at current offset */
        OS_$DATA_COPY(src,
                      (uint8_t *)((*cur_buf)->data) + *buf_offset,
                      chunk_size);

        src += chunk_size;
        remaining -= chunk_size;

        if (remaining == 0) {
            /* All data copied, update offset in current buffer */
            *buf_offset += chunk_size;
        } else {
            /* Buffer full, move to next buffer */
            *buf_offset = 0;
            *cur_buf = (*cur_buf)->next;
        }
    }

    *src_va_ptr = ARCH_PTR_TO_VA(src);
}

void MAC_$RECEIVE(uint16_t *channel, mac_$recv_pkt_t *pkt_desc, status_$t *status_ret)
{
    uint16_t chan;
    uint32_t chan_offset;
    uint16_t flags;
    uint8_t owner_asid;
    uint16_t socket_num;
    status_$t cleanup_status;

    /*
     * A6-0x40: the record SOCK_$GET fills in.  Every field this routine
     * touches lands on a sock_$pkt_info_t offset: hdr 0x00 (-0x40),
     * src_addr 0x04 (-0x3c), src_port 0x08 (-0x38), dst_addr 0x0c (-0x34),
     * flags 0x10 (the byte at -0x2f), n_hops 0x12 (-0x2e), hops 0x14
     * (-0x2c), data_len 0x2a (-0x16), hdr_len 0x2c (-0x14) and data_pages
     * 0x30 (-0x10).
     */
    sock_$pkt_info_t pkt_info;
    uint32_t secondary_buf;  /* A6-0x6c: VA of the payload page */

    /* Buffer info from packet header */
    uint16_t header_len;
    uint16_t data_len;
    uint32_t data_ppn;  /* Physical page number for data buffer */
    uint32_t data_va;   /* Virtual address for data buffer */

    /* Buffer chain tracking */
    mac_$buffer_t *buf_ptr;
    mac_$buffer_t *cur_buf;
    int16_t buf_offset;
    int32_t total_buf_size;

    uint8_t cleanup_buf[24];  /* FIM cleanup handler context */

    *status_ret = status_$ok;
    secondary_buf = 0;

#if defined(ARCH_M68K)
    chan = *channel;

    /*
     * Validate channel number.
     * Channel must be < 10.
     */
    if (chan >= MAC_MAX_CHANNELS) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /* Calculate channel table offset: chan * 20 */
    chan_offset = (uint32_t)chan * 20;

    /* Read flags from channel entry at offset 0x7B2 */
    flags = *(uint16_t *)(MAC_$DATA_BASE + 0x7B2 + chan_offset);

    /* Check if channel is open (bit 9 / 0x200) */
    if ((flags & 0x200) == 0) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * Check ownership:
     * - If bit 8 (0x100) set, shared access allowed
     * - Otherwise, owner ASID (bits 2-7 >> 2) must match current ASID
     */
    if ((flags & 0x100) == 0) {
        owner_asid = (flags & 0xFC) >> 2;
        if (owner_asid != PROC1_$AS_ID) {
            *status_ret = status_$mac_channel_not_open;
            return;
        }
    }

    /* Get socket number from channel entry at offset 0x7A8 */
    socket_num = *(uint16_t *)(MAC_$DATA_BASE + 0x7A8 + chan_offset);

    /* Check if socket is allocated */
    if (socket_num == MAC_NO_SOCKET) {
        *status_ret = status_$mac_no_socket_allocated;
        return;
    }

    /*
     * Get next packet from socket.
     * SOCK_$GET returns negative on success.
     */
    if (SOCK_$GET(socket_num, &pkt_info) >= 0) {
        *status_ret = status_$mac_no_packet_available_to_receive;
        return;
    }

    /*
     * Extract packet info from the retrieved packet.
     * Set arp_flag (broadcast indicator) from bit 0 of flags byte.
     */
    /* 0x00E0BE42: btst.b #0,(-0x2f,A6) / sne - bit 0 of the flags word */
    pkt_desc->arp_flag = (pkt_info.flags & 1) ? -1 : 0;

    /* 0x00E0BE52: move.w (-0x2e,A6),(A3) */
    pkt_desc->num_packet_types = (int16_t)pkt_info.n_hops;

    /*
     * 0x00E0BE5C: both pointers walk FORWARD ("addq.l #0x2,A0" and
     * "addq.l #0x2,A1"), copying n_hops words from the record's hop array
     * into pkt_desc + 0x02.
     */
    {
        int16_t i;
        int16_t count = pkt_desc->num_packet_types;

        for (i = 0; i < count; i++) {
            pkt_desc->packet_types[i] = pkt_info.hops[i];
        }
    }

    /* 0x00E0BE6E - 0x00E0BE7A */
    *(uint32_t *)((uint8_t *)pkt_desc + 0x2A) = pkt_info.src_addr;
    *(int16_t *)((uint8_t *)pkt_desc + 0x2E) = (int16_t)pkt_info.src_port;
    *(uint32_t *)((uint8_t *)pkt_desc + 0x30) = pkt_info.dst_addr;

    /*
     * Set up cleanup handler for fault recovery.
     */
    cleanup_status = FIM_$CLEANUP(cleanup_buf);
    if (cleanup_status != status_$cleanup_handler_set) {
        /* Cleanup triggered - return buffers and exit */
        /*
         * 0x00E0BFB4: pea (-0x40,A6) / pea (-0x6c,A6) / pea (-0x10,A6).
         * The third argument is the record's payload page array, not NULL;
         * the first cast restates its 32-bit header VA cell.
         */
        NETBUF_$RTN_PKT(&pkt_info.hdr, &secondary_buf,
                        pkt_info.data_pages, (int16_t)pkt_info.data_len);
        *status_ret = cleanup_status;
        return;
    }

    /*
     * Walk the user's buffer chain to calculate total available space.
     * Also validate each buffer entry.
     */
    buf_ptr = (mac_$buffer_t *)((uint8_t *)pkt_desc + 0x1C);  /* buffers field */
    cur_buf = buf_ptr;
    total_buf_size = 0;

    while (cur_buf != NULL) {
        /* Validate buffer: size must not be negative */
        if (cur_buf->size < 0) {
            *status_ret = status_$mac_illegal_buffer_spec;
            goto cleanup_and_return;
        }

        /* Validate: if size > 0, data pointer must be non-null */
        if (cur_buf->size > 0 && cur_buf->data == NULL) {
            *status_ret = status_$mac_illegal_buffer_spec;
            goto cleanup_and_return;
        }

        total_buf_size += cur_buf->size;
        cur_buf = cur_buf->next;
    }

    /*
     * Get header and data lengths from packet buffer.
     * header_len at offset -0x14, data_len at offset -0x16
     */
    header_len = pkt_info.hdr_len;   /* record +0x2c, "move.w (-0x14,A6)" */
    data_len = pkt_info.data_len;    /* record +0x2a, "move.w (-0x16,A6)" */

    /* Check if buffers are large enough */
    if (total_buf_size < (int32_t)(header_len + data_len)) {
        *status_ret = status_$mac_received_packet_too_big;
        goto cleanup_and_return;
    }

    /*
     * If there's data in a secondary buffer, get its virtual address.
     */
    if (data_len != 0) {
        /* 0x00E0BF0C: move.l (-0x10,A6),-(SP) - the page VA, by value */
        data_ppn = pkt_info.data_pages[0];
        NETBUF_$GETVA(data_ppn, &secondary_buf, status_ret);
        if (*status_ret != status_$ok) {
            secondary_buf = 0;
            goto cleanup_and_return;
        }
    }

    /*
     * Copy data to user buffers.
     * First copy header data, then body data.
     */
    cur_buf = buf_ptr;
    buf_offset = 0;

    if (header_len != 0) {
        /* 0x00E0BF3C: pea (-0x40,A6) - the helper advances the record's own
         * header cell, so it must be passed by reference. */
        copy_to_buffers(&pkt_info.hdr, header_len, &cur_buf, &buf_offset);
    }

    if (data_len != 0) {
        /* 0x00E0BF52: pea (-0x6c,A6) - likewise the payload VA cell. */
        copy_to_buffers(&secondary_buf, data_len, &cur_buf, &buf_offset);
    }

    /*
     * Clear remaining buffer sizes to indicate end of data.
     */
    while (cur_buf != NULL) {
        cur_buf->size = 0;
        cur_buf = cur_buf->next;
    }

    *status_ret = status_$ok;

cleanup_and_return:
    /* Return packet buffers to the pool - 0x00E0BF8C, same shape as above */
    NETBUF_$RTN_PKT(&pkt_info.hdr, &secondary_buf,
                    pkt_info.data_pages, (int16_t)pkt_info.data_len);

    /* Release cleanup handler */
    FIM_$RLS_CLEANUP(cleanup_buf);

#else
    /* Non-M68K implementation stub */
    (void)chan;
    (void)chan_offset;
    (void)flags;
    (void)owner_asid;
    (void)socket_num;
    (void)cleanup_status;
    (void)pkt_info;
    (void)secondary_buf;
    (void)header_len;
    (void)data_len;
    (void)data_ppn;
    (void)data_va;
    (void)buf_ptr;
    (void)cur_buf;
    (void)buf_offset;
    (void)total_buf_size;
    (void)cleanup_buf;
    *status_ret = status_$mac_channel_not_open;
#endif
}
