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
    void **src_ptr,      /* Pointer to source data pointer (updated) */
    int16_t length,      /* Number of bytes to copy */
    mac_$buffer_t **cur_buf,  /* Pointer to current buffer (updated) */
    int16_t *buf_offset  /* Pointer to offset in current buffer (updated) */
)
{
    uint8_t *src = (uint8_t *)*src_ptr;
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

    *src_ptr = src;
}

void MAC_$RECEIVE(uint16_t *channel, mac_$recv_pkt_t *pkt_desc, status_$t *status_ret)
{
    uint16_t chan;
    uint32_t chan_offset;
    uint16_t flags;
    uint8_t owner_asid;
    uint16_t socket_num;
    status_$t cleanup_status;

    /* Local packet info from socket get */
    uint8_t pkt_buf[64];  /* Buffer for packet header from socket */
    uint32_t secondary_buf;  /* Secondary buffer for packet data */

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
    if (SOCK_$GET(socket_num, (void **)pkt_buf) >= 0) {
        *status_ret = status_$mac_no_packet_available_to_receive;
        return;
    }

    /*
     * Extract packet info from the retrieved packet.
     * Set arp_flag (broadcast indicator) from bit 0 of flags byte.
     */
    {
        uint8_t pkt_flags = pkt_buf[0x2F - 0x40 + 0x40];  /* Offset adjustment */
        pkt_desc->arp_flag = (pkt_flags & 1) ? -1 : 0;
    }

    /* Copy packet type info */
    pkt_desc->num_packet_types = *(int16_t *)(pkt_buf + 0x2E - 0x40 + 0x40);

    /* Copy packet type values */
    {
        int16_t *src = (int16_t *)(pkt_buf + 0x2C - 0x40 + 0x40);
        int16_t *dst = (int16_t *)pkt_desc->packet_types;
        int16_t i;
        int16_t count = pkt_desc->num_packet_types;

        for (i = count - 1; i >= 0; i--) {
            dst[i] = src[-i];  /* Copy in reverse, adjusting offsets */
        }
    }

    /* Copy additional header fields */
    *(uint32_t *)((uint8_t *)pkt_desc + 0x2A) = *(uint32_t *)(pkt_buf + 0x3C - 0x40 + 0x40);
    *(int16_t *)((uint8_t *)pkt_desc + 0x2E) = *(int16_t *)(pkt_buf + 0x38 - 0x40 + 0x40);
    *(uint32_t *)((uint8_t *)pkt_desc + 0x30) = *(uint32_t *)(pkt_buf + 0x34 - 0x40 + 0x40);

    /*
     * Set up cleanup handler for fault recovery.
     */
    cleanup_status = FIM_$CLEANUP(cleanup_buf);
    if (cleanup_status != status_$cleanup_handler_set) {
        /* Cleanup triggered - return buffers and exit */
        NETBUF_$RTN_PKT(pkt_buf, &secondary_buf, NULL, data_len);
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
    header_len = *(uint16_t *)(pkt_buf + 0x14);  /* Placeholder offset */
    data_len = *(uint16_t *)(pkt_buf + 0x16);    /* Placeholder offset */

    /* Check if buffers are large enough */
    if (total_buf_size < (int32_t)(header_len + data_len)) {
        *status_ret = status_$mac_received_packet_too_big;
        goto cleanup_and_return;
    }

    /*
     * If there's data in a secondary buffer, get its virtual address.
     */
    if (data_len != 0) {
        data_ppn = *(uint32_t *)(pkt_buf + 0x10);  /* PPN at offset -0x10 */
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
        void *hdr_ptr = (void *)(*(uint32_t *)pkt_buf);  /* Header data pointer */
        copy_to_buffers(&hdr_ptr, header_len, &cur_buf, &buf_offset);
    }

    if (data_len != 0) {
        void *data_ptr = (void *)secondary_buf;
        copy_to_buffers(&data_ptr, data_len, &cur_buf, &buf_offset);
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
    /* Return packet buffers to the pool */
    NETBUF_$RTN_PKT(pkt_buf, &secondary_buf, NULL, data_len);

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
    (void)pkt_buf;
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
