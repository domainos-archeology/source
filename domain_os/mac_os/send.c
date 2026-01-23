/*
 * MAC_OS_$SEND - Send a packet at OS level
 *
 * Prepares and sends a packet through the driver. Sets up network
 * buffers for the header and data portions.
 *
 * Original address: 0x00E0B5A8
 * Original size: 620 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$SEND
 *
 * This function handles packet transmission including:
 * - Validating the driver supports send
 * - Setting up FIM cleanup handler
 * - Allocating network buffers for header and data
 * - Calling the driver's send callback
 * - Releasing buffers on completion or error
 *
 * Parameters:
 *   channel    - Pointer to channel number (0-9)
 *   pkt_desc   - Packet descriptor containing:
 *                0x1C: Buffer chain for body data
 *                0x28: Flags (bit 7 = needs buffer setup)
 *                0x38: Data length
 *                0x3C: Data buffer pointer (set by this function)
 *   bytes_sent - Pointer to receive bytes sent count
 *   status_ret - Pointer to receive status code
 *
 * Assembly notes:
 *   - Uses A5 = 0xE22990 (MAC_OS_$DATA base)
 *   - Packet sizes: small <= 0x3B8, medium <= 0x400, large > 0x400
 *   - Large packets require two buffers (header + data)
 */
void MAC_OS_$SEND(int16_t *channel, mac_os_$send_pkt_t *pkt_desc,
                  int16_t *bytes_sent, status_$t *status_ret)
{
#if defined(M68K)
    int16_t chan_num;
    mac_os_$channel_t *chan;
    uint32_t channel_offset;
    void *driver_info;
    status_$t cleanup_status;
    int8_t needs_buffers;
    int8_t use_header_buf;  /* True if we need header buffer */
    int8_t use_data_buf;    /* True if we need separate data buffer */
    int16_t total_length;
    int16_t overflow_length;
    uint8_t cleanup_info[24];  /* FIM cleanup info on stack */

    /* Local buffer state */
    int32_t header_ptr;
    int32_t data_ptr;
    status_$t buffer_status;
    void *data_buf_handle;

    *bytes_sent = 0;
    *status_ret = status_$ok;
    header_ptr = 0;
    data_ptr = 0;

    chan_num = *channel;

    /* Calculate channel table offset */
    channel_offset = (uint32_t)chan_num * MAC_OS_CHANNEL_SIZE;
    chan = (mac_os_$channel_t *)(MAC_OS_$DATA_BASE + 0x7A0 + channel_offset);

    /* Check if driver has send callback (offset 0x44) */
    driver_info = chan->driver_info;
    if (*(void **)((uint8_t *)driver_info + MAC_OS_DRIVER_SEND_OFFSET) == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* Set up FIM cleanup handler */
    cleanup_status = FIM_$CLEANUP(cleanup_info);
    if (cleanup_status != status_$cleanup_handler_set) {
        /* Cleanup was triggered - return buffers */
        if (needs_buffers < 0) {
            NETBUF_$RTN_PKT(&header_ptr, &data_ptr,
                           (void *)((uint8_t *)pkt_desc + 0x3C), total_length);
        }
        *status_ret = cleanup_status;
        return;
    }

    /* Check if we need to set up buffers (flag at pkt_desc+0x28 bit 7) */
    needs_buffers = ~(*(int8_t *)((uint8_t *)pkt_desc + 0x28));

    if (needs_buffers >= 0) {
        /* No buffer setup needed, go directly to send */
        goto do_send;
    }

    /* Validate buffer chain and calculate total length */
    use_header_buf = -1;  /* True */
    use_data_buf = -1;    /* True */
    total_length = 0;
    overflow_length = 0;

    {
        int32_t *buffer_chain = (int32_t *)((uint8_t *)pkt_desc + 0x1C);

        while (buffer_chain != NULL) {
            int32_t buf_size = buffer_chain[0];

            /* Validate buffer size */
            if (buf_size < 0) {
                goto buffer_error;
            }
            if (buf_size > 0 && buffer_chain[1] == 0) {
                /* Non-zero size but null data pointer */
                goto buffer_error;
            }

            total_length += (int16_t)buf_size;
            buffer_chain = (int32_t *)buffer_chain[2];  /* Next buffer */
        }
    }

    /* Check total length doesn't exceed maximum */
    if (total_length > MAC_OS_MAX_PACKET_SIZE) {
buffer_error:
        FIM_$RLS_CLEANUP(cleanup_info);
        *status_ret = status_$mac_illegal_buffer_spec;
        return;
    }

    /* Determine buffer allocation strategy based on size */
    if (total_length == 0) {
        /* Empty packet - no buffers needed */
        use_header_buf = 0;
        use_data_buf = 0;
    } else if (total_length <= MAC_OS_SMALL_PACKET_SIZE) {
        /* Small packet - fits in header buffer only */
        use_data_buf = 0;
    } else if (total_length <= MAC_OS_LARGE_PACKET_SIZE) {
        /* Medium packet - fits in data buffer only */
        use_header_buf = 0;
    }
    /* Large packet (> 0x400) - needs both buffers */

    /* Reset buffer tracking for copy */
    overflow_length = 0;
    /* buffer_chain is pkt_desc+0x1C */

    /* Get header buffer */
    NETBUF_$GET_HDR(cleanup_info + 4, &header_ptr);
    header_ptr += chan->header_size;

    if (use_header_buf < 0) {
        if (use_data_buf < 0) {
            /* Large packet - split at 0x400 boundary */
            overflow_length = total_length - MAC_OS_LARGE_PACKET_SIZE;
            total_length = MAC_OS_LARGE_PACKET_SIZE;
        } else {
            /* All data goes to header buffer */
            total_length = 0;
            overflow_length = total_length;
        }

        /* Copy data to header buffer */
        MAC_OS_$COPY_BUFFER_DATA(&header_ptr, overflow_length);
    }

    if (use_data_buf < 0) {
        /* Get data buffer */
        NETBUF_$GET_DAT(&data_buf_handle);
        NETBUF_$GETVA(data_buf_handle, &data_ptr, &buffer_status);

        if (buffer_status != status_$ok) {
            *status_ret = buffer_status;
            data_ptr = 0;
            goto send_done;
        }

        /* Store data buffer handle in packet descriptor */
        *(void **)((uint8_t *)pkt_desc + 0x3C) = data_buf_handle;

        /* Copy remaining data to data buffer */
        MAC_OS_$COPY_BUFFER_DATA(&data_ptr, total_length);

        /* Return the VA */
        NETBUF_$RTNVA(&data_ptr);
        data_ptr = 0;
    } else {
        /* No data buffer used */
        *(void **)((uint8_t *)pkt_desc + 0x3C) = NULL;
    }

    /* Update packet descriptor with buffer info */
    *(int32_t *)((uint8_t *)pkt_desc + 0x1C) = overflow_length;
    *(int32_t *)((uint8_t *)pkt_desc + 0x20) = header_ptr;
    *(int32_t *)((uint8_t *)pkt_desc + 0x24) = 0;
    *(int32_t *)((uint8_t *)pkt_desc + 0x38) = total_length;

do_send:
    /* Call driver send callback */
    {
        void (*driver_send)(uint16_t, void *, void *, int16_t *, status_$t *);
        driver_send = *(void **)((uint8_t *)driver_info + MAC_OS_DRIVER_SEND_OFFSET);
        (*driver_send)(chan->line_number, &chan->callback_data, pkt_desc, bytes_sent, status_ret);
    }

send_done:
    /* Return buffers if we allocated them */
    if (needs_buffers < 0) {
        NETBUF_$RTN_PKT(&header_ptr, &data_ptr,
                       (void *)((uint8_t *)pkt_desc + 0x3C), total_length);
    }

    FIM_$RLS_CLEANUP(cleanup_info);
#else
    /* Non-M68K implementation stub */
    (void)channel;
    (void)pkt_desc;
    *bytes_sent = 0;
    *status_ret = status_$mac_port_op_not_implemented;
#endif
}
