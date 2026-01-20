/*
 * XNS IDP Demultiplexing
 *
 * Implementation of packet demultiplexing for incoming IDP packets.
 *
 * Original addresses:
 *   XNS_IDP_$OS_DEMUX:          0x00E184A8
 *   XNS_IDP_$DEMUX:             0x00E18B8A
 *   XNS_IDP_$OS_ADD_PORT:       0x00E1872C
 *   XNS_IDP_$OS_DELETE_PORT:    0x00E1876C
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$OS_DEMUX - Demultiplex incoming packet (OS-level)
 *
 * Called by the MAC layer when an IDP packet is received. This function:
 *   1. Validates the packet checksum
 *   2. Determines if packet is for us or needs forwarding
 *   3. Finds the target channel based on socket number
 *   4. Delivers to the channel's demux callback
 *
 * @param packet_info   Packet information structure
 * @param port_ptr      Pointer to receiving port number
 * @param param3        Additional parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E184A8
 */
void XNS_IDP_$OS_DEMUX(void *packet_info, int16_t *port_ptr, void *param3, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    uint8_t *pkt = (uint8_t *)packet_info;
    int16_t *header;
    int8_t is_local;
    int16_t dest_socket;
    uint16_t channel;
    route_$port_t *rport;
    status_$t local_status;

    *status_ret = status_$ok;

    /* Increment received counter */
    *(uint32_t *)(base + XNS_OFF_PACKETS_RECV) += 1;

    /* Get packet header pointer */
    header = *(int16_t **)(pkt + 0x20);

    /* Check if destination is broadcast or not for us */
    if (header[0x0D] == -1 && header[0x0B] == -1 && header[0x0C] == -1) {
        /* Broadcast - drop if no local socket */
        *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
        *status_ret = status_$xns_no_route;
        return;
    }

    /* Validate checksum */
    if (header[0] != -1) {
        int16_t computed = xns_$get_checksum(packet_info);
        if (computed != header[0]) {
            /* Checksum mismatch - send error */
            uint8_t local_params[0x88];
            uint16_t error_code;
            uint16_t error_param;
            uint8_t dummy[10];

            /* Set up error params */
            *(uint32_t *)(local_params + 0x62) = *(uint32_t *)(pkt + 0x2A);
            *(uint16_t *)(local_params + 0x5E) = *(uint16_t *)(pkt + 0x2E);
            *(uint32_t *)(local_params + 0x70) = *(uint32_t *)(pkt + 0x1C);
            *(uint32_t *)(local_params + 0x74) = *(uint32_t *)(pkt + 0x20);
            *(uint32_t *)(local_params + 0x78) = *(uint32_t *)(pkt + 0x24);
            local_params[0x64] = 0xFF;

            /* Copy extra info */
            {
                int16_t i;
                for (i = 0; i < 20; i++) {
                    local_params[0x54 + i] = pkt[0x38 + i];
                }
            }

            /* Check if destination is local or broadcast */
            is_local = xns_$is_broadcast_addr((uint8_t *)(header + 3));
            if (is_local < 0) {
                error_code = XNS_ERROR_BAD_CHECKSUM;
            } else {
                error_code = XNS_ERROR_BAD_CHECKSUM;  /* Same code */
            }
            error_param = 0;

            XNS_ERROR_$SEND(local_params, &error_code, &error_param, dummy, &local_status);
            *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
            *status_ret = status_$xns_bad_checksum;
            return;
        }
    }

    /* Get port info */
    rport = ROUTE_$PORTP[*port_ptr];

    /* Check if destination is local */
    is_local = xns_$is_broadcast_addr((uint8_t *)(header + 3));

    if (is_local < 0) {
        /* Destination is local - find channel by socket */
        dest_socket = header[8];

        if (dest_socket != -1 && dest_socket != 0) {
            /* Look up channel by socket number */
            uint16_t found_channel = XNS_MAX_CHANNELS;
            int16_t i;

            for (i = 0; i < XNS_MAX_CHANNELS; i++) {
                uint8_t *chan = base + i * XNS_CHANNEL_SIZE;
                if (*(uint16_t *)(chan + XNS_CHAN_OFF_XNS_SOCKET) == dest_socket) {
                    found_channel = i;
                    break;
                }
            }

            if (found_channel == XNS_MAX_CHANNELS ||
                *(uint32_t *)(base + found_channel * XNS_CHANNEL_SIZE + XNS_CHAN_OFF_DEMUX) == 0) {
                *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
                *status_ret = status_$xns_no_route;
                return;
            }

            /* Set up callback parameters */
            {
                uint8_t callback_params[0x88];
                code_ptr_t callback;
                uint8_t *chan = base + found_channel * XNS_CHANNEL_SIZE;

                *(uint32_t *)(callback_params + 0x62) = *(uint32_t *)(pkt + 0x2A);
                *(uint16_t *)(callback_params + 0x5E) = *(uint16_t *)(pkt + 0x2E);
                *(void **)(callback_params + 0x58) = (void *)(base + found_channel * XNS_CHANNEL_SIZE + XNS_CHAN_OFF_DEMUX);
                *(uint32_t *)(callback_params + 0x70) = *(uint32_t *)(pkt + 0x1C);
                *(uint32_t *)(callback_params + 0x74) = *(uint32_t *)(pkt + 0x20);
                *(uint32_t *)(callback_params + 0x78) = *(uint32_t *)(pkt + 0x24);
                callback_params[0x64] = 0xFF;

                /* Copy extra info */
                {
                    int16_t j;
                    for (j = 0; j < 20; j++) {
                        callback_params[0x54 + j] = pkt[0x38 + j];
                    }
                }

                /* Call channel's demux callback */
                callback = (code_ptr_t)*(uint32_t *)(chan + XNS_CHAN_OFF_DEMUX);
                ((void (*)(void *, void *, void *, void *, status_$t *))callback)
                    (callback_params,
                     (void *)((uint8_t *)rport + 0x2E),
                     (void *)((uint8_t *)rport + 0x30),
                     param3,
                     status_ret);

                if (*status_ret == status_$ok) {
                    return;
                }
                *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
            }
        }
    } else {
        /* Destination not local - need to forward (routing) */
        extern uint16_t ROUTE_$STD_N_ROUTING_PORTS;

        if (ROUTE_$STD_N_ROUTING_PORTS < 2) {
            /* No routing configured */
            *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
            *status_ret = status_$xns_no_route;
            return;
        }

        /* Check hop count */
        if (*(uint8_t *)((uint8_t *)header + 4) >= 15) {
            /* Too many hops */
            *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
            *status_ret = status_$xns_hop_count_exceeded;
            return;
        }

        /* Forward packet via routing socket */
        {
            struct {
                int16_t flags;
                uint32_t mac_info1;
                uint16_t mac_info2;
                uint32_t mac_info3;
                uint16_t header_len;
                uint16_t port_info;
                int16_t *header_ptr;
                uint16_t extra[16];
            } forward_params;

            forward_params.flags = 2;
            forward_params.mac_info1 = *(uint32_t *)(pkt + 0x2A);
            forward_params.mac_info2 = *(uint16_t *)(pkt + 0x2E);
            forward_params.mac_info3 = *(uint32_t *)(pkt + 0x30);
            forward_params.header_ptr = header;
            forward_params.header_len = *(uint16_t *)(pkt + 0x1E);
            forward_params.port_info = *(uint16_t *)(pkt + 0x3A);

            /* Copy MAC info */
            {
                int16_t k;
                for (k = 0; k < 16; k++) {
                    forward_params.extra[k] = *(uint16_t *)(pkt + 0x3C + k * 2);
                }
            }

            /* Put to routing socket */
            {
                extern uint16_t ROUTE_$SOCK;
                int8_t result = SOCK_$PUT(ROUTE_$SOCK, &forward_params, 0,
                                          *(uint16_t *)((uint8_t *)rport + 0x2E),
                                          *(uint16_t *)((uint8_t *)rport + 0x30));
                if (result >= 0) {
                    *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
                    *status_ret = status_$xns_packet_dropped;
                    return;
                }
            }
        }
    }

    *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
    *status_ret = status_$xns_no_route;
}

/*
 * XNS_IDP_$DEMUX - Demultiplex incoming packet (user-level callback)
 *
 * Default demux callback for user channels. Queues the packet to
 * the channel's user socket.
 *
 * @param packet_info   Packet information
 * @param port_hi       Port high word pointer
 * @param port_lo       Port low word pointer
 * @param flags         Flags pointer
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18B8A
 */
void XNS_IDP_$DEMUX(void *packet_info, uint16_t *port_hi, uint16_t *port_lo,
                    char *flags, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    uint8_t *pkt = (uint8_t *)packet_info;
    int16_t *header;
    uint16_t channel_flags = 2;
    void *channel_demux_ptr;
    uint16_t user_socket;

    *status_ret = status_$ok;

    /* Get header pointer */
    header = *(int16_t **)(pkt + 0x1C);

    /* Check if destination is broadcast */
    if (header[0x0E / 2] == -1 && header[0x0A / 2] == -1 && header[0x0C / 2] == -1) {
        channel_flags |= 1;  /* Broadcast flag */
    }

    if (*flags < 0) {
        channel_flags |= 4;  /* Additional flag from caller */
    }

    /* Build socket put parameters */
    {
        struct {
            uint32_t mac_info1;
            uint16_t mac_info2;
            uint32_t length;
            int16_t *header_ptr;
            uint16_t header_len;
            uint16_t port_info;
            uint16_t extra[16];
            uint16_t flags;
        } sock_params;

        sock_params.mac_info1 = *(uint32_t *)(pkt + 0x26);
        sock_params.mac_info2 = *(uint16_t *)(pkt + 0x2A);
        sock_params.length = *(uint32_t *)(pkt + 0x2C);
        sock_params.header_ptr = header;
        sock_params.header_len = *(uint16_t *)(pkt + 0x1A);
        sock_params.port_info = *(uint16_t *)(pkt + 0x36);

        /* Copy extra fields */
        {
            int16_t i;
            for (i = 0; i < 16; i++) {
                sock_params.extra[i] = *(uint16_t *)(pkt + 0x38 + i * 2);
            }
        }
        sock_params.flags = 0;

        /* Get channel's user socket */
        channel_demux_ptr = *(void **)(pkt + 0x30);
        user_socket = *(uint16_t *)((uint8_t *)channel_demux_ptr + 0x36);

        if (user_socket == XNS_NO_SOCKET) {
            *status_ret = status_$xns_no_route;
            return;
        }

        /* Queue to user socket */
        {
            int8_t result = SOCK_$PUT(user_socket, &sock_params, 0, *port_hi, *port_lo);
            if (result >= 0) {
                *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) += 1;
                *status_ret = status_$xns_packet_dropped;
            }
        }
    }
}

/*
 * XNS_IDP_$OS_ADD_PORT - Add a port to a channel (OS-level)
 *
 * @param channel_ptr   Pointer to channel number
 * @param port_ptr      Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1872C
 */
void XNS_IDP_$OS_ADD_PORT(uint16_t *channel_ptr, uint16_t *port_ptr, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;

    ML_$EXCLUSION_START((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
    xns_$add_port(*channel_ptr, *port_ptr, status_ret);
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
}

/*
 * XNS_IDP_$OS_DELETE_PORT - Delete a port from a channel (OS-level)
 *
 * @param channel_ptr   Pointer to channel number
 * @param port_ptr      Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1876C
 */
void XNS_IDP_$OS_DELETE_PORT(uint16_t *channel_ptr, uint16_t *port_ptr, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;

    ML_$EXCLUSION_START((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
    xns_$delete_port(*channel_ptr, *port_ptr, status_ret);
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
}
