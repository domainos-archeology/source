/*
 * XNS IDP Send Operations
 *
 * Implementation of XNS_IDP_$SEND and XNS_IDP_$OS_SEND for
 * sending IDP packets.
 *
 * Original addresses:
 *   XNS_IDP_$SEND:     0x00E18A66
 *   XNS_IDP_$OS_SEND:  0x00E18256
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$OS_SEND - Send a packet (OS-level)
 *
 * Low-level packet send operation used by both kernel and user-level code.
 * This function:
 *   1. Validates parameters
 *   2. Builds the IDP header (if not already built)
 *   3. Finds the route to destination
 *   4. Computes checksum (if needed)
 *   5. Sends via MAC layer
 *
 * @param channel_ptr   Pointer to channel number
 * @param send_params   Send parameters structure:
 *                      +0x00-0x17: Destination address (24 bytes)
 *                      +0x18: header_len
 *                      +0x1C: header_ptr
 *                      +0x20: iov chain
 *                      +0x24: flags
 *                      +0x2D: packet_type
 *                      +0x34-0x47: extra fields
 *                      +0x36: checksum control
 * @param checksum_ret  Output: computed checksum
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18256
 */
void XNS_IDP_$OS_SEND(int16_t *channel_ptr, void *send_params, uint16_t *checksum_ret,
                      status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    uint8_t *params = (uint8_t *)send_params;
    int16_t channel = *channel_ptr;
    int iVar1;
    int8_t is_connected;
    int8_t is_broadcast;
    int16_t *header_ptr;
    int header_len;
    int total_len;
    int16_t port;
    uint8_t mac_info[24];
    status_$t local_status;
    uint8_t cleanup_buf[24];

    *checksum_ret = 0;
    *status_ret = status_$ok;

    /* Set up cleanup handler */
    local_status = FIM_$CLEANUP(cleanup_buf);
    if (local_status != status_$cleanup_handler_set) {
        *status_ret = local_status;
        return;
    }

    /* Get channel base address */
    iVar1 = channel * XNS_CHANNEL_SIZE;

    /* Check if connected mode */
    is_connected = (*(uint8_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) & 0x20) ? -1 : 0;

    /* Check if "no header build" mode */
    if (*(uint8_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) & 0x08) {
        /* Header already built, just need to fill in destination */
        header_ptr = *(int16_t **)(params + 0x1C);
        header_ptr[0] = -1;  /* Checksum = none initially */

        /* Calculate total length from iov chain */
        header_len = *(int32_t *)(params + 0x18);
        {
            int32_t *iov = *(int32_t **)(params + 0x20);
            total_len = header_len;

            while (iov != NULL) {
                int32_t len = iov[0];
                if (len < 0 || (len > 0 && iov[1] == 0)) {
                    FIM_$RLS_CLEANUP(cleanup_buf);
                    *status_ret = status_$xns_invalid_param;
                    return;
                }
                total_len += len;
                iov = (int32_t *)iov[2];  /* Next in chain */
            }
        }

        /* Set packet length */
        header_ptr[1] = *(int16_t *)(params + 0x36) + (int16_t)total_len;

        /* Set transport control and packet type */
        *(uint8_t *)((uint8_t *)header_ptr + 4) = 0;
        *(uint8_t *)((uint8_t *)header_ptr + 5) = params[0x2D];

        /* Fill in addresses */
        if (is_connected < 0) {
            /* Use channel's stored addresses */
            *(uint32_t *)(header_ptr + 3) = *(uint32_t *)(base + iVar1 + XNS_CHAN_OFF_DEST_NETWORK);
            *(uint32_t *)(header_ptr + 5) = *(uint32_t *)(base + iVar1 + XNS_CHAN_OFF_DEST_NETWORK + 4);
            *(uint32_t *)(header_ptr + 7) = *(uint32_t *)(base + iVar1 + XNS_CHAN_OFF_DEST_NETWORK + 8);
            *(uint32_t *)(header_ptr + 9) = *(uint32_t *)(base + iVar1 + XNS_CHAN_OFF_SRC_NETWORK);
            *(uint32_t *)(header_ptr + 0xB) = *(uint32_t *)(base + iVar1 + XNS_CHAN_OFF_SRC_HOST);
            *(uint32_t *)(header_ptr + 0xD) = *(uint32_t *)(base + iVar1 + XNS_CHAN_OFF_SRC_HOST + 4);
        } else {
            /* Copy from send_params */
            int16_t i;
            uint8_t *src = params;
            uint8_t *dst = (uint8_t *)(header_ptr + 3);
            for (i = 0; i < 24; i++) {
                *dst++ = *src++;
            }
        }
    }

    /* Get destination and find route */
    if (is_connected < 0) {
        /* Connected mode - use stored port and MAC info */
        port = *(int16_t *)(base + iVar1 + XNS_CHAN_OFF_CONN_PORT);
        {
            uint8_t *src = base + iVar1 + XNS_CHAN_OFF_MAC_INFO;
            uint8_t *dst = mac_info;
            int16_t i;
            for (i = 0; i < 24; i++) {
                *dst++ = *src++;
            }
        }
    } else {
        /* Look up route to destination */
        int16_t nexthop_port[4];
        uint8_t nexthop_info[16];
        uint32_t dest_addr[3];

        header_ptr = *(int16_t **)(params + 0x1C);
        dest_addr[0] = *(uint32_t *)(header_ptr + 3);
        dest_addr[1] = *(uint32_t *)(header_ptr + 5);
        dest_addr[2] = *(uint32_t *)(header_ptr + 7);

        RIP_$FIND_NEXTHOP((int16_t *)dest_addr, 0xFF, nexthop_port, nexthop_info, status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        port = nexthop_port[0];
        if (port == -1) {
            FIM_$RLS_CLEANUP(cleanup_buf);
            *status_ret = status_$xns_no_nexthop;
            return;
        }

        MAC_OS_$ARP(nexthop_info, port, mac_info, NULL, status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }
    }

    /* Add port to channel if not already */
    xns_$add_port(channel, port, status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Build MAC send parameters */
    {
        struct {
            uint32_t ethertype;
            int32_t header_len;
            void *header_ptr;
            void *iov;
            uint8_t flags;
            uint8_t extra[0x14];
        } mac_send_params;

        mac_send_params.ethertype = 0x600;  /* XNS ethertype */
        mac_send_params.header_len = *(int32_t *)(params + 0x18);
        mac_send_params.header_ptr = *(void **)(params + 0x1C);
        mac_send_params.iov = *(void **)(params + 0x20);
        mac_send_params.flags = params[0x24];

        /* Copy extra fields */
        {
            int16_t i;
            for (i = 0; i < 0x14; i++) {
                mac_send_params.extra[i] = params[0x34 + i];
            }
        }

        /* Compute checksum if needed */
        header_ptr = *(int16_t **)(params + 0x1C);
        if (header_ptr[0] != -1) {
            /* Checksum already set or disabled */
        } else {
            int16_t csum = xns_$get_checksum(mac_info);
            header_ptr[0] = csum;
            if (csum == -1) {
                FIM_$RLS_CLEANUP(cleanup_buf);
                *status_ret = status_$xns_bad_checksum;
                return;
            }
        }

        /* Send via MAC layer */
        {
            route_$port_t *rport = ROUTE_$PORTP[port];
            uint8_t *port_addr = (uint8_t *)rport + 0x48;
            MAC_OS_$SEND(port_addr, mac_info, checksum_ret, status_ret);
        }

        if (*status_ret == status_$ok) {
            /* Increment packets sent counter */
            *(uint32_t *)base += 1;
        }
    }

cleanup:
    FIM_$RLS_CLEANUP(cleanup_buf);
}

/*
 * XNS_IDP_$SEND - Send a packet (user-level)
 *
 * User-level wrapper for packet send. Validates ownership and
 * calls XNS_IDP_$OS_SEND.
 *
 * @param channel_ptr   Pointer to channel number
 * @param send_params   Send parameters structure (xns_$idp_send_t)
 * @param checksum_ret  Output: computed checksum
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18A66
 */
void XNS_IDP_$SEND(uint16_t *channel_ptr, xns_$idp_send_t *send_params,
                   uint16_t *checksum_ret, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    uint16_t channel = *channel_ptr;
    int iVar1;
    uint8_t cleanup_buf[24];
    status_$t local_status;

    /* Local copy of send parameters */
    uint8_t local_params[0x48];
    uint16_t local_checksum;
    status_$t local_status2;

    *checksum_ret = 0;
    *status_ret = status_$ok;

    /* Validate channel number and ownership */
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

    /* Check ownership (AS_ID must match) */
    {
        uint16_t chan_as_id = (*(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) &
                               XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT;
        if (chan_as_id != PROC1_$AS_ID) {
            *status_ret = status_$xns_bad_channel;
            return;
        }
    }

    /* Set up cleanup handler */
    local_status = FIM_$CLEANUP(cleanup_buf);
    if (local_status != status_$cleanup_handler_set) {
        *status_ret = local_status;
        return;
    }

    /* Validate send parameters */
    if (*(void **)(send_params + 0x1C) == NULL ||
        *(int32_t *)(send_params + 0x18) < XNS_IDP_HEADER_SIZE) {
        FIM_$RLS_CLEANUP(cleanup_buf);
        *status_ret = status_$xns_invalid_param;
        return;
    }

    /* Copy destination address portion */
    {
        int16_t i;
        uint8_t *src = (uint8_t *)send_params;
        uint8_t *dst = local_params;
        for (i = 0; i < 24; i++) {
            *dst++ = *src++;
        }
    }

    /* Copy packet type and other fields */
    *(uint16_t *)(local_params + 0x1C) = ((uint8_t *)send_params)[0x2C];
    *(uint32_t *)(local_params + 0x14) = 0;
    *(uint32_t *)(local_params + 0x10) = 0;

    /* Copy header/iov info */
    *(uint32_t *)(local_params + 0x18) = *(uint32_t *)((uint8_t *)send_params + 0x18);
    *(uint32_t *)(local_params + 0x1C) = *(uint32_t *)((uint8_t *)send_params + 0x1C);
    *(uint32_t *)(local_params + 0x20) = *(uint32_t *)((uint8_t *)send_params + 0x20);
    local_params[0x24] = 0;

    /* Clear iov flags */
    {
        xns_$idp_iov_t *iov = *(xns_$idp_iov_t **)((uint8_t *)send_params + 0x20);
        while (iov != NULL) {
            iov->flags = 0;
            iov = iov->next;
        }
    }

    /* Call OS-level send */
    XNS_IDP_$OS_SEND((int16_t *)channel_ptr, local_params, &local_checksum, &local_status2);
    *checksum_ret = local_checksum;
    *status_ret = local_status2;

    FIM_$RLS_CLEANUP(cleanup_buf);
}
