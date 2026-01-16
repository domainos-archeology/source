/*
 * PKT_$SEND_INTERNET - Send an internet packet
 *
 * Sends a packet over the network. Handles retries on failure.
 *
 * The algorithm:
 * 1. Check data length (max 0x200 = 512 bytes)
 * 2. Copy data to physical address buffers if present
 * 3. Get retry count from pkt_info (0 = unlimited, else use value)
 * 4. Loop sending until success or retries exhausted:
 *    a. Get header buffer from NETWORK_$GETHDR
 *    b. Build header via PKT_$BLD_INTERNET_HDR
 *    c. Send via NET_IO_$SEND
 *    d. On failure, return header and wait before retry
 *
 * Original address: 0x00E1264E
 */

#include "pkt/pkt_internal.h"

/* Wait time for retry: 25000 ticks (25 ms at 1000 ticks/sec) */
#define PKT_RETRY_WAIT_TICKS    25000

/* Placeholder event count for waiting */
static ec_$eventcount_t pkt_wait_ec;

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                        int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *len_out, void *extra, status_$t *status_ret)
{
    uint32_t data_buffers[PKT_MAX_DATA_CHUNKS];
    uint16_t hdr_len[3];
    int16_t port;
    uint32_t hdr_buf[64];  /* Header buffer - 256 bytes max */
    uint32_t hdr_va;
    uint32_t hdr_pa;
    status_$t local_status;
    uint16_t retry_count;
    uint16_t max_retries;
    uint16_t param15, param16;
    uint16_t delay_type;
    clock_t wait_delay;
    status_$t wait_status;

    data_buffers[0] = 0;

    /* Check data length limit */
    if (template_len > 0x200) {
        *status_ret = status_$network_message_header_too_big;
        return;
    }

    /* Copy data to physical address buffers if present */
    if (data_len > 0) {
        PKT_$COPY_TO_PA((char *)data, data_len, data_buffers, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
    }

    /* Get retry count from pkt_info structure */
    /* Offset 0x08 contains retry count; 0 means use default (0xFFFF = unlimited) */
    if (*(int16_t *)((char *)pkt_info + 8) == 0) {
        max_retries = 0xFFFF;
    } else {
        max_retries = *(uint16_t *)((char *)pkt_info + 8);
    }

    retry_count = 0;

    do {
        retry_count++;

        /* Get a header buffer */
        NETWORK_$GETHDR(&dest_node, &hdr_va, &hdr_pa);

        /* Build the packet header */
        PKT_$BLD_INTERNET_HDR(routing_key, dest_node, dest_sock,
                              src_node_or, src_node, src_sock,
                              pkt_info, request_id,
                              template, template_len, data_len,
                              &port, (uint32_t *)hdr_va, hdr_len,
                              &param15, &param16, &local_status);

        if (local_status != status_$ok) {
            break;
        }

        /* Update max_retries on first successful header build */
        if (max_retries == 0xFFFF) {
            max_retries = *len_out;
        }

        /* Send the packet */
        NET_IO_$SEND(port, &hdr_va, hdr_pa, hdr_len[0], 0,
                     data_buffers, data_len, PKT_$DATA->default_flags,
                     extra, &local_status);

        if (local_status == status_$ok) {
            /* Success - we're done */
            break;
        }

        /* Send failed - return header and maybe retry */
        NETWORK_$RTNHDR(&hdr_va);
        hdr_va = 0;

        /* Check for specific error conditions */
        /* If both extra params are 0 and 0x2000, don't retry */
        if (param15 == 0 && param16 == 0x2000) {
            break;
        }

        /* Wait before retry */
        delay_type = 0;  /* Relative delay */
        wait_delay.high = 0;
        wait_delay.low = PKT_RETRY_WAIT_TICKS;
        TIME_$WAIT(&delay_type, &wait_delay, &wait_status);

        /* Check for quit signal */
        if (wait_status == 0x000D0003) {  /* Quit requested */
            local_status = wait_status;
            break;
        }

    } while (retry_count < max_retries);

    /* Return header buffer if still allocated */
    if (hdr_va != 0) {
        NETWORK_$RTNHDR(&hdr_va);
    }

    /* Release data buffers */
    PKT_$DUMP_DATA(data_buffers, data_len);

    *status_ret = local_status;
}
