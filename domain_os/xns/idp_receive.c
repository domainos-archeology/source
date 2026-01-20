/*
 * XNS IDP Receive Operations
 *
 * Implementation of XNS_IDP_$RECEIVE for receiving IDP packets.
 *
 * Original address: 0x00E18CE2
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$RECEIVE - Receive a packet (user-level)
 *
 * Receives an IDP packet from the specified channel. The packet data
 * is copied to the caller's buffer(s) via an iov chain.
 *
 * @param channel_ptr   Pointer to channel number
 * @param recv_params   Receive parameters structure:
 *                      +0x18: iov chain for data
 *                      +0x1C: header buffer
 *                      +0x26: output: MAC info (6 bytes)
 *                      +0x2C: output: packet type
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18CE2
 */
void XNS_IDP_$RECEIVE(uint16_t *channel_ptr, void *recv_params, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    uint8_t *params = (uint8_t *)recv_params;
    uint16_t channel = *channel_ptr;
    int iVar1;
    uint8_t cleanup_buf[24];
    status_$t local_status;

    /* Socket get result */
    struct {
        int32_t header_ptr;
        uint32_t mac_info1;
        uint16_t mac_info2;
        uint16_t header_len;
        uint16_t data_len;
        void *extra_ptr[4];
    } sock_result;

    void *extra_buf = NULL;

    *status_ret = status_$ok;
    sock_result.header_ptr = 0;

    /* Validate channel number and access */
    if (channel >= XNS_MAX_CHANNELS) {
        *status_ret = status_$xns_bad_channel;
        return;
    }

    iVar1 = channel * XNS_CHANNEL_SIZE;

    /* Check channel is active */
    if (*(int16_t *)(base + iVar1 + XNS_CHAN_OFF_STATE) >= 0) {
        *status_ret = status_$xns_bad_channel;
        return;
    }

    /* Check access: either broadcast receive flag set, or AS_ID matches */
    if (!(*(uint8_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) & 0x80)) {
        /* Not broadcast - check AS_ID */
        uint16_t chan_as_id = (*(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) &
                               XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT;
        if (chan_as_id != PROC1_$AS_ID) {
            *status_ret = status_$xns_bad_channel;
            return;
        }
    }

    /* Check that user socket is allocated */
    {
        uint16_t user_socket = *(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_USER_SOCKET);
        if (user_socket == XNS_NO_SOCKET) {
            *status_ret = status_$xns_no_socket;
            return;
        }

        /* Try to get a packet from the socket */
        int8_t result = SOCK_$GET(user_socket, &sock_result);
        if (result >= 0) {
            *status_ret = status_$xns_no_data;
            return;
        }
    }

    /* Packet received - copy data to caller */
    {
        int16_t *header = (int16_t *)sock_result.header_ptr;

        /* Copy source address to recv_params if "no header build" mode */
        if (*(uint8_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) & 0x08) {
            uint8_t *src = (uint8_t *)header + 6;
            uint8_t *dst = params;
            int16_t i;
            for (i = 0; i < 24; i++) {
                *dst++ = *src++;
            }
            /* Copy packet type */
            *(uint16_t *)(params + 0x2C) = *(uint8_t *)((uint8_t *)header + 5);
        }

        /* Copy MAC info */
        *(uint32_t *)(params + 0x26) = sock_result.mac_info1;
        *(uint16_t *)(params + 0x2A) = sock_result.mac_info2;

        /* Set up cleanup handler */
        local_status = FIM_$CLEANUP(cleanup_buf);
        if (local_status != status_$cleanup_handler_set) {
            NETBUF_$RTN_PKT(&sock_result, extra_buf, sock_result.extra_ptr, sock_result.data_len);
            *status_ret = local_status;
            return;
        }

        /* Validate receive buffer */
        if (*(void **)(params + 0x1C) == NULL) {
            *status_ret = status_$xns_invalid_param;
            goto cleanup;
        }

        /* Check buffer size */
        {
            int32_t *iov = (int32_t *)(params + 0x18);
            int32_t total_size = 0;
            int32_t *iov_ptr = iov;

            while (iov_ptr != NULL) {
                int32_t len = iov_ptr[0];
                if (len < 0 || (len > 0 && iov_ptr[1] == 0)) {
                    *status_ret = status_$xns_invalid_param;
                    goto cleanup;
                }
                total_size += len;
                iov_ptr = (int32_t *)iov_ptr[2];
            }

            if (total_size < (int32_t)(sock_result.header_len + sock_result.data_len)) {
                *status_ret = status_$xns_buffer_too_small;
                goto cleanup;
            }

            /* Get virtual address for extra data if needed */
            if (sock_result.data_len != 0) {
                NETBUF_$GETVA(sock_result.extra_ptr[0], &extra_buf, status_ret);
                if (*status_ret != status_$ok) {
                    extra_buf = NULL;
                    goto cleanup;
                }
            }

            /* Copy data to iov chain */
            iov_ptr = iov;

            if (sock_result.header_len != 0) {
                xns_$copy_packet_data(&sock_result, sock_result.header_len);
            }

            if (sock_result.data_len != 0) {
                xns_$copy_packet_data(&extra_buf, sock_result.data_len);
            }

            /* Clear remaining iov entries */
            while (iov_ptr != NULL) {
                iov_ptr[0] = 0;
                iov_ptr = (int32_t *)iov_ptr[2];
            }

            *status_ret = status_$ok;
        }

cleanup:
        NETBUF_$RTN_PKT(&sock_result, extra_buf, sock_result.extra_ptr, sock_result.data_len);
        FIM_$RLS_CLEANUP(cleanup_buf);
    }
}
