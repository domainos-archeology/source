/*
 * MAC_$DEMUX - Demultiplex received packet (callback)
 *
 * Internal callback function registered with the socket layer.
 * Routes incoming packets to the appropriate channel based on
 * packet type filters. Called by the network driver when a packet arrives.
 *
 * Original address: 0x00E0BC4E
 * Original size: 222 bytes
 */

#include "mac/mac_internal.h"

/*
 * The original function receives a packet descriptor from the lower layer
 * and queues it to the appropriate MAC channel's socket.
 *
 * Parameters (from analysis):
 *   param_1 (pkt_info) - Pointer to packet header info structure
 *     - offset 0x00: num_packet_types (short)
 *     - offset 0x02+: packet type values (shorts)
 *     - offset 0x18: arp_flag (char, bit 7 indicates broadcast/multicast)
 *     - offset 0x20: header data
 *     - offset 0x2A: additional header info
 *     - offset 0x2E: field
 *     - offset 0x30: field
 *     - offset 0x34: channel pointer
 *     - offset 0x3A: field (short)
 *     - offset 0x3C-0x4C: additional data
 *
 *   param_2 (port_info) - Pointer to short containing port number
 *
 *   param_3 (flags) - Pointer to char flags
 *     - bit 7: indicates some condition
 *
 *   status_ret - Status return pointer
 */
void MAC_$DEMUX(void *pkt_info, int16_t *port_info, char *flags, status_$t *status_ret)
{
    int16_t *pkt = (int16_t *)pkt_info;
    uint16_t demux_flags;
    int16_t num_types;
    int16_t i;
    void *route_port_ptr;
    void *channel_ptr;
    uint16_t socket_num;
    uint16_t ec_param1;
    uint16_t ec_param2;

    *status_ret = status_$ok;

    /*
     * Build demux flags:
     * - Start with 2 (always set)
     * - If pkt->arp_flag (offset 0x18) bit 7 set, set flag bit 0 (becomes 3)
     * - If *flags bit 7 set, set flag bit 2 (becomes 6 or 7)
     */
    demux_flags = 2;
    if (*(int8_t *)((uint8_t *)pkt_info + 0x18) < 0) {
        demux_flags |= 1;  /* Broadcast/multicast */
    }
    if (*flags < 0) {
        demux_flags |= 4;
    }

    /*
     * Copy packet type values to local frame.
     * Original copies pkt->num_packet_types shorts from offset 0x02.
     */
    num_types = pkt[0];

    /*
     * Build local packet descriptor on stack.
     * The original code builds a structure with:
     * - offset 0x30 (-0x30 from A6): demux_flags
     * - offset 0x2E (-0x2E): num_packet_types
     * - offset 0x2C-0x1A: packet type values
     * - offset 0x3C (-0x3C): data from pkt+0x2A
     * - offset 0x38 (-0x38): data from pkt+0x2E
     * - offset 0x34 (-0x34): data from pkt+0x30
     * - offset 0x40 (-0x40): data from pkt+0x20
     * - offset 0x14 (-0x14): data from pkt+0x1E
     * - offset 0x16 (-0x16): data from pkt+0x3A
     * - offset 0x10-0x00 (-0x10 to -0x00): data from pkt+0x3C
     */

#if defined(M68K)
    /*
     * Get port info from ROUTE_$PORTP array.
     * The port number from port_info is used as index.
     */
    {
        int16_t port_num = *port_info;
        route_port_ptr = (void *)(*(uint32_t *)(0xE26EE8 + port_num * 4));
    }

    /*
     * Get channel pointer from packet info (offset 0x34).
     * This points to the channel entry.
     */
    channel_ptr = *(void **)((uint8_t *)pkt_info + 0x34);

    /* Get socket number from channel (offset 0x08 in channel structure) */
    socket_num = *(uint16_t *)((uint8_t *)channel_ptr + 0x08);

    /* Check if socket is allocated (not 0xE1) */
    if (socket_num == MAC_NO_SOCKET) {
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /*
     * Get EC parameters from route port info.
     * ec_param1 at offset 0x2E, ec_param2 at offset 0x30.
     */
    ec_param1 = *(uint16_t *)((uint8_t *)route_port_ptr + 0x2E);
    ec_param2 = *(uint16_t *)((uint8_t *)route_port_ptr + 0x30);

    /*
     * Queue the packet to the socket.
     * SOCK_$PUT returns negative on success.
     *
     * The packet descriptor pointer passed is the local structure built
     * on the stack, starting at offset -0x40 from frame pointer.
     */
    if (SOCK_$PUT(socket_num, pkt_info, 0, ec_param1, ec_param2) >= 0) {
        *status_ret = status_$mac_failed_to_put_packet_into_socket;
    }

#else
    /* Non-M68K stub */
    (void)pkt;
    (void)demux_flags;
    (void)num_types;
    (void)i;
    (void)route_port_ptr;
    (void)channel_ptr;
    (void)socket_num;
    (void)ec_param1;
    (void)ec_param2;
    *status_ret = status_$mac_channel_not_open;
#endif
}
