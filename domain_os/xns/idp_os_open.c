/*
 * XNS IDP OS-Level Channel Management
 *
 * Implementation of XNS_IDP_$OS_OPEN and XNS_IDP_$OS_CLOSE for
 * OS-level (kernel-internal) IDP channel management.
 *
 * Original addresses:
 *   XNS_IDP_$OS_OPEN:  0x00E17F02
 *   XNS_IDP_$OS_CLOSE: 0x00E181D8
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$OS_OPEN - Open an IDP channel (OS-level)
 *
 * Opens a new IDP channel for kernel-level use. This function:
 *   1. Validates the socket number is available
 *   2. Finds a free channel slot
 *   3. Handles port binding (if requested)
 *   4. Sets up connected mode (if requested)
 *   5. Assigns the socket number (dynamic if 0)
 *
 * Open options flags (at offset +3 of options):
 *   Bit 1 (0x02): Bind to specific local port
 *   Bit 2 (0x04): Connected mode (specific destination)
 *   Bit 3 (0x08): No socket allocation (internal use)
 *
 * @param options       Pointer to open options structure:
 *                      +0x00: socket number (0 = dynamic)
 *                      +0x02: channel index return (for OS_OPEN)
 *                      +0x03: flags byte
 *                      +0x04: demux callback
 *                      +0x08: local port (if bind flag set)
 *                      +0x0C-0x17: destination address (if connect flag)
 *                      +0x18-0x23: source address (if connect flag)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17F02
 */
void XNS_IDP_$OS_OPEN(void *options, status_$t *status_ret)
{
    int16_t *opt = (int16_t *)options;
    uint8_t *base = XNS_IDP_BASE;
    int16_t socket = opt[0];
    uint8_t flags = *((uint8_t *)options + 3);
    uint16_t channel;
    uint8_t *chan_base;
    int16_t port;
    int8_t local_addr_ok = 0;

    *status_ret = status_$ok;

    /* Check channel limit */
    if (XNS_OPEN_COUNT() >= XNS_MAX_CHANNELS) {
        *status_ret = status_$xns_too_many_channels;
        return;
    }

    /* Validate socket number (if non-zero) */
    if (socket != 0) {
        int8_t in_use = xns_$find_socket(socket);
        if (in_use < 0) {
            *status_ret = status_$xns_socket_in_use;
            return;
        }
    }

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START((ml_$exclusion_t *)(base + XNS_OFF_LOCK));

    /* Find a free channel slot */
    channel = 0;
    chan_base = base;
    while (*(int16_t *)(chan_base + XNS_CHAN_OFF_STATE) < 0) {
        /* Channel is active (bit 15 set) */
        if (channel >= XNS_MAX_CHANNELS) {
            *status_ret = status_$xns_channel_table_full;
            goto cleanup_error;
        }
        channel++;
        chan_base = base + channel * XNS_CHANNEL_SIZE;
    }

    /* Handle bind-to-local-port flag */
    if (flags & XNS_OPEN_FLAG_BIND_LOCAL) {
        int32_t local_port = *(int32_t *)(opt + 4);  /* offset +0x08 */

        if (local_port == -1) {
            /* Bind to all ports */
            local_addr_ok = 0;
            for (port = 0; port < XNS_MAX_PORTS; port++) {
                xns_$add_port(channel, port, status_ret);
                if (*status_ret == status_$ok) {
                    local_addr_ok = -1;  /* At least one succeeded */
                }
            }
            /* If at least one port bound and status is recoverable error, clear it */
            if (local_addr_ok < 0 &&
                (*status_ret == status_$internet_network_port_not_open ||
                 *status_ret == status_$mac_port_op_not_implemented)) {
                *status_ret = status_$ok;
            }
            if (*status_ret != status_$ok) {
                goto cleanup_error;
            }
        } else {
            /* Bind to specific port */
            int16_t port_num[2];
            MAC_$NET_TO_PORT_NUM((int16_t *)(opt + 4), port_num);
            port = port_num[0];
            if (port == -1) {
                *status_ret = status_$xns_unknown_network_port;
                goto cleanup_error;
            }
            xns_$add_port(channel, port, status_ret);
            if (*status_ret != status_$ok) {
                goto cleanup_error;
            }
        }
    }

    /* Handle connected mode flag */
    if (flags & XNS_OPEN_FLAG_CONNECT) {
        uint8_t *dest_addr = (uint8_t *)options + 0x18;  /* Destination at offset 0x18 */

        /* Check if destination is broadcast (all zeros or all ones = error) */
        if (*(int32_t *)(dest_addr) == 0 &&
            *(int16_t *)(dest_addr + 0x0B) == 0 &&
            *(int16_t *)(dest_addr + 4) == 0 &&
            *(int16_t *)(dest_addr + 6) == 0 &&
            *(int16_t *)(dest_addr + 8) == 0) {
            local_addr_ok = -1;  /* Use local address */
        } else {
            /* Check if destination is in our address table */
            int8_t is_local = xns_$is_broadcast_addr(dest_addr);
            if (is_local >= 0) {
                *status_ret = status_$xns_local_addr_in_use;
                goto cleanup_error;
            }
        }

        /* Find next hop to destination */
        {
            int16_t nexthop_port[4];
            uint8_t nexthop_info[16];

            RIP_$FIND_NEXTHOP((int16_t *)dest_addr, 0xFF, nexthop_port, nexthop_info, status_ret);
            if (*status_ret != status_$ok) {
                goto cleanup_error;
            }

            port = nexthop_port[0];
            if (port == -1) {
                *status_ret = status_$xns_no_nexthop;
                goto cleanup_error;
            }

            /* Perform ARP to get MAC address */
            {
                int iVar1 = channel * XNS_CHANNEL_SIZE;
                MAC_OS_$ARP(nexthop_info, port,
                            (uint8_t *)(base + iVar1 + XNS_CHAN_OFF_MAC_INFO),
                            NULL, status_ret);
                if (*status_ret != status_$ok) {
                    goto cleanup_error;
                }
            }

            /* Add the port to this channel */
            xns_$add_port(channel, port, status_ret);
            if (*status_ret != status_$ok) {
                goto cleanup_error;
            }

            /* Copy destination address to channel state */
            {
                uint8_t *chan = base + channel * XNS_CHANNEL_SIZE;
                *(uint32_t *)(chan + XNS_CHAN_OFF_DEST_NETWORK) = *(uint32_t *)(dest_addr);
                *(uint32_t *)(chan + XNS_CHAN_OFF_DEST_NETWORK + 4) = *(uint32_t *)(dest_addr + 4);
                *(uint32_t *)(chan + XNS_CHAN_OFF_DEST_NETWORK + 8) = *(uint32_t *)(dest_addr + 8);
                *(int16_t *)(chan + XNS_CHAN_OFF_CONN_PORT) = port;

                if (local_addr_ok < 0) {
                    /* Use routing port's network address as source */
                    route_$port_t *rport = ROUTE_$PORTP[port];
                    *(uint32_t *)(chan + XNS_CHAN_OFF_SRC_NETWORK) = rport->network;

                    /* Copy local host address */
                    *(uint16_t *)(chan + XNS_CHAN_OFF_SRC_HOST) = *(uint16_t *)(base + XNS_OFF_LOCAL_SOCKET);
                    *(uint16_t *)(chan + XNS_CHAN_OFF_SRC_HOST + 2) = *(uint16_t *)(base + XNS_OFF_LOCAL_HOST_HI);
                    *(uint16_t *)(chan + XNS_CHAN_OFF_SRC_HOST + 4) = *(uint16_t *)(base + XNS_OFF_LOCAL_HOST_LO);

                    /* Source port = our socket */
                    *(uint16_t *)(chan + XNS_CHAN_OFF_SRC_PORT) =
                        *(uint16_t *)(chan + XNS_CHAN_OFF_XNS_SOCKET);
                } else {
                    /* Use provided source address */
                    uint8_t *src_addr = (uint8_t *)options + 0x0C;
                    *(uint32_t *)(chan + XNS_CHAN_OFF_SRC_NETWORK) = *(uint32_t *)(src_addr);
                    *(uint32_t *)(chan + XNS_CHAN_OFF_SRC_HOST) = *(uint32_t *)(src_addr + 4);
                    *(uint32_t *)(chan + XNS_CHAN_OFF_SRC_HOST + 4) = *(uint32_t *)(src_addr + 8);
                }
            }
        }
    }

    /* Assign socket number */
    if (socket == 0) {
        /* Assign dynamic socket number */
        socket = XNS_NEXT_SOCKET();
        opt[0] = socket;

        do {
            XNS_NEXT_SOCKET() += 1;
            if (XNS_NEXT_SOCKET() >= 0xFFFE) {
                XNS_NEXT_SOCKET() = XNS_FIRST_DYNAMIC_PORT;
            }
        } while (xns_$find_socket(XNS_NEXT_SOCKET()) < 0);
    }

    /* Set up channel state */
    {
        uint8_t *chan = base + channel * XNS_CHANNEL_SIZE;

        XNS_OPEN_COUNT() += 1;

        /* Mark channel as active */
        *(uint8_t *)(chan + XNS_CHAN_OFF_STATE) |= 0x80;

        /* Set socket number */
        *(uint16_t *)(chan + XNS_CHAN_OFF_XNS_SOCKET) = socket;

        /* Set user socket to "none" */
        *(uint16_t *)(chan + XNS_CHAN_OFF_USER_SOCKET) = XNS_NO_SOCKET;

        /* Set demux callback */
        *(uint32_t *)(chan + XNS_CHAN_OFF_DEMUX) = *(uint32_t *)(opt + 2);  /* +0x04 */

        /* Set flags */
        *(uint8_t *)(chan + XNS_CHAN_OFF_FLAGS) &= 0x07;
        *(uint8_t *)(chan + XNS_CHAN_OFF_FLAGS) |= (flags << 3);

        /* Set owning AS_ID */
        {
            uint8_t as_id = (uint8_t)PROC1_$AS_ID;
            *(uint16_t *)(chan + XNS_CHAN_OFF_FLAGS) &= ~XNS_CHAN_FLAG_AS_ID_MASK;
            *(uint16_t *)(chan + XNS_CHAN_OFF_FLAGS) |= (as_id << XNS_CHAN_FLAG_AS_ID_SHIFT);
        }
    }

    /* Return channel index */
    opt[1] = channel;
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
    return;

cleanup_error:
    /* Clear channel state on error */
    chan_base = base + channel * XNS_CHANNEL_SIZE;
    *(uint8_t *)(chan_base + XNS_CHAN_OFF_STATE) &= 0x7F;
    *(uint32_t *)(chan_base + XNS_CHAN_OFF_DEMUX) = 0;
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
}

/*
 * XNS_IDP_$OS_CLOSE - Close an IDP channel (OS-level)
 *
 * Closes a previously opened IDP channel and releases all resources.
 * This includes:
 *   1. Removing all port bindings
 *   2. Clearing channel state
 *   3. Decrementing open channel count
 *
 * @param channel_ptr   Pointer to channel number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E181D8
 */
void XNS_IDP_$OS_CLOSE(int16_t *channel_ptr, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    int16_t channel = *channel_ptr;
    int iVar1;
    int16_t port;

    *status_ret = status_$ok;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START((ml_$exclusion_t *)(base + XNS_OFF_LOCK));

    /* Decrement open channel count */
    XNS_OPEN_COUNT() -= 1;

    /* Calculate channel offset */
    iVar1 = channel * XNS_CHANNEL_SIZE;

    /* Delete all active port bindings */
    for (port = 0; port < XNS_MAX_PORTS; port++) {
        if (*(int8_t *)(base + iVar1 + XNS_CHAN_OFF_PORT_ACTIVE + port) < 0) {
            xns_$delete_port(channel, port, status_ret);
        }
    }

    /* Clear channel state */
    *(uint8_t *)(base + iVar1 + XNS_CHAN_OFF_STATE) &= 0x7F;         /* Clear active flag */
    *(uint8_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) &= 0x07;         /* Clear flags */
    *(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_XNS_SOCKET) = 0;       /* Clear socket */
    *(uint32_t *)(base + iVar1 + XNS_CHAN_OFF_DEMUX) = 0;            /* Clear callback */

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
}
