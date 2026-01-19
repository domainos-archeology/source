/*
 * ROUTE_$OUTGOING - Handle outgoing routed packets
 *
 * This function retrieves queued outgoing packets from user routing ports
 * and prepares them for transmission. It finds the appropriate routing
 * next hop, copies packet data, and optionally computes a checksum.
 *
 * Original address: 0x00E87A4E
 * Size: 486 bytes
 */

#include "route/route_internal.h"
#include "sock/sock.h"
#include "rip/rip.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"
#include "os/os.h"

/*
 * ROUTE_$CHECKSUM_ENABLED - Flag to enable packet checksumming
 *
 * When the high bit is set (negative), a simple checksum is computed
 * over the packet data.
 *
 * Original address: 0xE88218
 */
#if defined(M68K)
    #define ROUTE_$CHECKSUM_ENABLED (*(int8_t *)0xE88218)
#else
    extern int8_t ROUTE_$CHECKSUM_ENABLED;
#endif

/* Maximum packet data length (0x7FC = 2044 bytes) */
#define ROUTE_$MAX_PACKET_DATA  0x7FC

/* Status code for port not in user mode */
#define status_$route_port_not_user_mode    0x2B0001

/*
 * ROUTE_$OUTGOING - Retrieve and prepare outgoing routed packet
 *
 * Called to dequeue an outgoing packet from a user routing port and
 * prepare it for transmission via the routing protocol.
 *
 * @param port_info     Port information (network at +6, socket at +8)
 * @param nexthop_ret   Output: next hop network address (20-bit) + flag
 * @param packet_buf    Output: packet data buffer (4-byte header + data)
 * @param length_ret    Output: total packet length including header
 * @param status_ret    Output: status code
 *
 * Output packet format:
 *   +0x00: 4-byte checksum/magic (0xDEC0DED base, modified by data)
 *   +0x04: packet data (copied from socket buffer)
 *
 * Status codes:
 *   status_$ok: Success
 *   status_$internet_unknown_network_port: Port not found
 *   status_$route_port_not_user_mode: Port not in user/routing mode
 *   status_$network_buffer_queue_is_empty: No packet queued
 *
 * Original address: 0x00E87A4E
 */
void ROUTE_$OUTGOING(void *port_info, uint32_t *nexthop_ret, uint8_t *packet_buf,
                     int16_t *length_ret, status_$t *status_ret)
{
    int16_t port_index;
    uint16_t network;
    int16_t socket;
    route_$port_t *port;
    void *sock_buf[12];       /* Socket buffer structure */
    void *pkt_chain[4];       /* Packet chain for additional data */
    uint8_t nexthop_info[6];  /* Next hop info from RIP */
    uint32_t dest_addr[2];    /* Destination address for nexthop lookup */
    int8_t sock_result;
    uint16_t hdr_len;         /* Header data length */
    uint16_t data_len;        /* Additional data length */
    uint32_t checksum;
    int16_t copy_len;
    int16_t i;

    /* Extract network and socket from port_info */
    network = *(uint16_t *)((uint8_t *)port_info + 6);
    socket = *(int16_t *)((uint8_t *)port_info + 8);

    /* Find the port by network/socket */
    port_index = ROUTE_$FIND_PORT(network, (int32_t)socket);
    if (port_index == -1) {
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Get pointer to port structure and check port mode.
     * The port must be in user routing mode (port_type == 2)
     * and the active field bits 0-1 must be 0.
     */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /* Check that bits 0-1 of active field are not set when masked */
    if (((1 << (port->active & 0x1F)) & 0x3) != 0) {
        *status_ret = status_$route_port_not_user_mode;
        return;
    }

    /* Port type must be 2 (routing) */
    if (port->port_type != ROUTE_PORT_TYPE_ROUTING) {
        *status_ret = status_$route_port_not_user_mode;
        return;
    }

    /* Get the socket buffer - returns negative on success */
    sock_result = SOCK_$GET(socket, sock_buf);
    if (sock_result >= 0) {
        *status_ret = status_$network_buffer_queue_is_empty;
        return;
    }

    /* Extract packet info from socket buffer */
    hdr_len = *(uint16_t *)((uint8_t *)sock_buf[0] + 0x10);
    data_len = *(uint16_t *)((uint8_t *)sock_buf[0] + 0x14);

    /* Set up destination address for nexthop lookup */
    dest_addr[0] = *(uint32_t *)((uint8_t *)sock_buf[0] + 0x2E);
    /* Extract 24-bit field and preserve upper bits */
    dest_addr[1] = (dest_addr[1] & 0xFFF00000) |
                   (*(uint32_t *)((uint8_t *)sock_buf[0] + 0x34) & 0x00FFFFFF);

    /* Find the routing next hop */
    RIP_$FIND_NEXTHOP(dest_addr, 0, nexthop_info, pkt_chain, status_ret);

    if (*status_ret != status_$ok) {
        /* Cleanup on failure */
        void *hdr_ptr[3];
        hdr_ptr[0] = sock_buf[0];
        NETBUF_$RTN_HDR(hdr_ptr);
        PKT_$DUMP_DATA(pkt_chain, data_len);
        return;
    }

    /* Extract 20-bit network address from nexthop result */
    *nexthop_ret = *(uint32_t *)((uint8_t *)pkt_chain - 0x5A + 0x60) & 0xFFFFF;

    /* Set flag byte based on socket buffer flags */
    *(uint8_t *)(nexthop_ret + 1) = (*(int8_t *)((uint8_t *)sock_buf[0] + 4) < 0) ? 0xFF : 0x00;

    /* Copy header data to output packet (after 4-byte checksum header) */
    OS_$DATA_COPY(sock_buf[0], packet_buf + 4, (uint32_t)hdr_len);

    /* Return the header buffer */
    {
        void *hdr_ptr[3];
        hdr_ptr[0] = sock_buf[0];
        NETBUF_$RTN_HDR(hdr_ptr);
    }

    /* Check if there's additional data in the packet chain */
    if (pkt_chain[0] == NULL) {
        data_len = 0;
    }

    if (data_len == 0) {
        copy_len = 0;
    } else {
        /* Calculate how much data to copy (max 0x7FC - header) */
        uint32_t max_copy = ROUTE_$MAX_PACKET_DATA - hdr_len;
        if ((uint32_t)data_len < max_copy) {
            copy_len = data_len;
        } else {
            copy_len = (int16_t)max_copy;
        }

        /* Copy additional packet data */
        PKT_$DAT_COPY(pkt_chain, copy_len, packet_buf + hdr_len + 4);

        /* Release the packet data buffers */
        PKT_$DUMP_DATA(pkt_chain, data_len);
    }

    /* Set total output length: checksum(4) + header + copied data */
    *length_ret = copy_len + hdr_len + 4;

    /* Compute checksum if enabled */
    checksum = 0x0DEC0DED;  /* Magic initial value */

    if (ROUTE_$CHECKSUM_ENABLED < 0) {
        int16_t checksum_len = copy_len + hdr_len - 1;
        if (checksum_len >= 0) {
            for (i = 4; checksum_len >= 0; i++, checksum_len--) {
                checksum = (uint32_t)packet_buf[i] + checksum * 0x11;
            }
        }
    }

    /* Copy checksum to packet header */
    OS_$DATA_COPY(&checksum, packet_buf, 4);
}
