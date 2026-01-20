/*
 * XNS IDP Internal Helper Functions
 *
 * Implementation of internal helper functions used by the XNS IDP module.
 *
 * Original addresses:
 *   xns_$find_socket:          0x00E17D12
 *   xns_$add_port:             0x00E17BF8
 *   xns_$delete_port:          0x00E17CB2
 *   xns_$get_checksum:         0x00E17D46
 *   xns_$is_broadcast_addr:    0x00E17E88
 *   xns_$is_local_addr:        0x00E17850
 *   xns_$copy_packet_data:     0x00E18C5E
 */

#include "xns/xns_internal.h"

/*
 * xns_$find_socket - Check if a socket number is already in use
 *
 * Scans all active channels to find if the given socket number
 * is already bound to an active channel.
 *
 * @param socket    Socket number to check
 *
 * @return 0xFF (-1 as signed char) if found (in use), 0 if not found (available)
 *
 * Original address: 0x00E17D12
 */
int8_t xns_$find_socket(int16_t socket)
{
    uint8_t *base = XNS_IDP_BASE;
    int16_t i;

    for (i = 0; i < XNS_MAX_CHANNELS; i++) {
        uint8_t *chan = base + i * XNS_CHANNEL_SIZE;

        /* Check if channel is active (bit 15 set in state) */
        if (*(int16_t *)(chan + XNS_CHAN_OFF_STATE) >= 0) {
            continue;  /* Not active */
        }

        /* Check if socket matches */
        if (*(int16_t *)(chan + XNS_CHAN_OFF_XNS_SOCKET) == socket) {
            return -1;  /* Found - socket is in use */
        }
    }

    return 0;  /* Not found - socket is available */
}

/*
 * xns_$add_port - Add a port to a channel's port list
 *
 * Adds the specified port to the channel's active port list.
 * If this is the first channel using this port, opens the MAC layer.
 *
 * @param channel       Channel index
 * @param port          Port number (0-7)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17BF8
 */
void xns_$add_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    int port_offset = port * XNS_PORT_STATE_SIZE;
    int chan_offset = channel * XNS_CHANNEL_SIZE;

    *status_ret = status_$ok;

    /* Check if port is already open at the port level */
    if (*(uint16_t *)(base + port_offset + XNS_PORT_OFF_REFCOUNT) == 0) {
        /* Port not yet open - need to open MAC layer */

        /* Check if port type supports opening */
        /* Access ROUTE port state to check type */
        {
            extern uint8_t DAT_00e2e0cc[];  /* Port type array */
            uint8_t port_type = DAT_00e2e0cc[port * 0x5C + 0x2C];
            if ((1 << (port_type & 0x1F)) & 0x3) {
                *status_ret = status_$internet_network_port_not_open;
                return;
            }
        }

        /* Open MAC layer for this port */
        {
            struct {
                code_ptr_t callback;
                uint16_t flags;
                uint32_t ethertype1;
                uint32_t ethertype2;
            } mac_open_params;

            mac_open_params.callback = (code_ptr_t)XNS_IDP_$OS_DEMUX;
            mac_open_params.flags = 1;
            mac_open_params.ethertype1 = 0x600;
            mac_open_params.ethertype2 = 0x600;

            MAC_OS_$OPEN(&port, &mac_open_params, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }

            /* Store MAC socket handle */
            *(uint16_t *)(base + port_offset + XNS_PORT_OFF_MAC_SOCKET) =
                (uint16_t)mac_open_params.ethertype2;
            *(uint32_t *)(base + port_offset + XNS_PORT_OFF_REF) = mac_open_params.ethertype1;
        }
    }

    /* Check if already added to this channel */
    if (*status_ret == status_$ok) {
        uint8_t *port_active = base + chan_offset + XNS_CHAN_OFF_PORT_ACTIVE + port;

        if (*port_active >= 0) {
            /* Not yet active for this channel - add it */
            *port_active = 0xFF;  /* Mark as active */
            *(uint16_t *)(base + port_offset + XNS_PORT_OFF_REFCOUNT) += 1;
        }
    }
}

/*
 * xns_$delete_port - Remove a port from a channel's port list
 *
 * Removes the specified port from the channel's active port list.
 * If this was the last channel using this port, closes the MAC layer.
 *
 * @param channel       Channel index
 * @param port          Port number (0-7)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17CB2
 */
void xns_$delete_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    int port_offset = port * XNS_PORT_STATE_SIZE;
    int chan_offset = channel * XNS_CHANNEL_SIZE;

    *status_ret = status_$ok;

    /* Clear port active flag for this channel */
    *(uint8_t *)(base + chan_offset + XNS_CHAN_OFF_PORT_ACTIVE + port) = 0;

    /* Decrement port reference count */
    uint16_t refcount = *(uint16_t *)(base + port_offset + XNS_PORT_OFF_REFCOUNT);
    refcount--;
    *(uint16_t *)(base + port_offset + XNS_PORT_OFF_REFCOUNT) = refcount;

    /* If no more references, close MAC layer */
    if (refcount == 0) {
        MAC_OS_$CLOSE((uint16_t *)(base + port_offset + XNS_PORT_OFF_MAC_SOCKET), status_ret);
        *(uint16_t *)(base + port_offset + XNS_PORT_OFF_MAC_SOCKET) = 0xFFFF;
    }
}

/*
 * xns_$get_checksum - Calculate checksum from packet info
 *
 * Extracts packet data and computes the IDP checksum.
 *
 * @param packet_info   Packet information structure (with header at +0x20)
 *
 * @return Calculated checksum, or -1 on error
 *
 * Original address: 0x00E17D46
 */
int16_t xns_$get_checksum(void *packet_info)
{
    uint8_t *pkt = (uint8_t *)packet_info;
    int16_t *header = *(int16_t **)(pkt + 0x20);
    uint16_t length = header[1];  /* Packet length at offset 2 */
    uint16_t word_count;

    /* Length includes header, compute word count */
    word_count = (length + 1) >> 1;  /* Round up to words */

    if (word_count == 0) {
        return -1;
    }

    return (int16_t)XNS_IDP_$CHECKSUM((uint16_t *)header, word_count);
}

/*
 * xns_$is_broadcast_addr - Check if address is a broadcast address
 *
 * Checks if the given XNS address is the broadcast address
 * (network -1, host -1, socket -1) or matches one of our
 * registered local addresses.
 *
 * @param addr      Pointer to 12-byte XNS address starting at network field
 *
 * @return 0xFF (-1) if broadcast or local, 0 if remote
 *
 * Original address: 0x00E17E88
 */
int8_t xns_$is_broadcast_addr(void *addr)
{
    uint8_t *base = XNS_IDP_BASE;
    int16_t *address = (int16_t *)addr;
    int16_t reg_count;
    int16_t i;

    /* Check for broadcast (all 0xFFFF) */
    if (address[4] == -1 && address[2] == -1 && address[3] == -1) {
        return -1;  /* Broadcast */
    }

    /* Check against registered addresses */
    for (i = 0; i < XNS_MAX_PORTS; i++) {
        route_$port_t *rport = ROUTE_$PORTP[i];
        if (rport == NULL) continue;

        /* Check if network matches */
        if (rport->network != *(uint32_t *)address) continue;

        /* Check registered addresses for this port */
        reg_count = XNS_REG_COUNT();
        if (reg_count >= 0) {
            int16_t j;
            for (j = 0; j <= reg_count; j++) {
                /* Check if host matches registered address */
                if (*(uint16_t *)(base + 0x24 + j * 6) == address[4] &&
                    *(uint16_t *)(base + 0x22 + j * 6) == address[3] &&
                    *(uint16_t *)(base + 0x20 + j * 6) == address[2]) {
                    return -1;  /* Local address */
                }
            }
        }
    }

    return 0;  /* Remote address */
}

/*
 * xns_$is_local_addr - Check if host portion is broadcast
 *
 * Validates that the host portion of an address is not the
 * broadcast address (all 0xFF bytes).
 *
 * @param addr      Pointer to host address (6 bytes)
 *
 * @return 0xFF (-1) if broadcast, 0 if OK
 *
 * Original address: 0x00E17850
 */
int8_t xns_$is_local_addr(void *addr)
{
    uint8_t *host = (uint8_t *)addr;

    /* Check if all bytes are 0xFF (broadcast) */
    if (host[0] == 0xFF && host[1] == 0xFF && host[2] == 0xFF &&
        host[3] == 0xFF && host[4] == 0xFF && host[5] == 0xFF) {
        return -1;  /* Broadcast */
    }

    return 0;  /* Not broadcast */
}

/*
 * xns_$copy_packet_data - Copy packet data to user buffer
 *
 * Helper function to copy received packet data to the user's
 * receive buffer(s). Used by XNS_IDP_$RECEIVE.
 *
 * @param iov_chain     Pointer to I/O vector chain state
 * @param length        Number of bytes to copy
 *
 * Original address: 0x00E18C5E
 */
void xns_$copy_packet_data(void *iov_chain, uint16_t length)
{
    /* This is a simplified implementation - the actual one
     * handles scatter-gather with multiple buffers */
    (void)iov_chain;
    (void)length;

    /* TODO: Implement proper scatter-gather copy */
}
