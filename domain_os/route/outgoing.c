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
 * ROUTE_$USER_CHECKSUM - Flag to enable packet checksumming
 *
 * When the high bit is set (negative), a simple checksum is computed
 * over the packet data.
 *
 * Original address: 0xE88218
 */
/* Declared in route/route_internal.h */

/* Maximum packet data length (0x7FC = 2044 bytes) */
#define ROUTE_$MAX_PACKET_DATA  0x7FC


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
 *   status_$internet_network_port_not_open: Port not in user/routing mode
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
    sock_$pkt_info_t rcv;     /* A6-0x50: SOCK_$GET's packet record */
    route_$internet_hdr_t *pkt;  /* A2: the dequeued header buffer */
    rip_$nexthop_t next_hop;  /* A6-0x60: RIP_$FIND_NEXTHOP's answer */
    int16_t nexthop_port;     /* A6-0x82: RIP_$FIND_NEXTHOP's port */
    uint32_t dest_addr[3];    /* A6-0x10: 12-byte destination for the lookup */
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
        *status_ret = status_$internet_network_port_not_open;
        return;
    }

    /* Port type must be 2 (routing) */
    if (port->port_type != ROUTE_PORT_TYPE_ROUTING) {
        *status_ret = status_$internet_network_port_not_open;
        return;
    }

    /* Get the socket buffer - returns negative on success (0x00E87ABC) */
    sock_result = SOCK_$GET(socket, &rcv);
    if (sock_result >= 0) {
        *status_ret = status_$network_buffer_queue_is_empty;
        return;
    }

    /* 0x00E87ADC - 0x00E87AE4: both lengths come out of the header buffer */
    pkt = (route_$internet_hdr_t *)rcv.hdr;
    hdr_len = pkt->hdr_len;
    data_len = pkt->data_len;

    /* 0x00E87AEA - 0x00E87B02: destination for the nexthop lookup */
    dest_addr[0] = *(uint32_t *)((uint8_t *)pkt + 0x2E);
    /* Extract 24-bit field and preserve upper bits */
    dest_addr[1] = (dest_addr[1] & 0xFFF00000) |
                   (*(uint32_t *)((uint8_t *)pkt + 0x34) & 0x00FFFFFF);

    /*
     * 0x00E87B06 - 0x00E87B1E.  The port goes to A6-0x82 and the 10-byte
     * next hop to A6-0x60; the data page vector (A6-0x20 = rcv.data_pages)
     * is a different buffer entirely.
     */
    RIP_$FIND_NEXTHOP(dest_addr, false, &nexthop_port, &next_hop, status_ret);

    if (*status_ret != status_$ok) {
        /* Cleanup on failure (0x00E87B26 - 0x00E87B44) */
        uint32_t hdr_va = (uint32_t)(uintptr_t)pkt;
        NETBUF_$RTN_HDR(&hdr_va);
        PKT_$DUMP_DATA(rcv.data_pages, (int16_t)data_len);
        return;
    }

    /* 0x00E87B48 - 0x00E87B54: the node id is nexthop+6 masked to 20 bits */
    *nexthop_ret = next_hop.host_lo & 0xFFFFF;

    /* 0x00E87B56 - 0x00E87B5C: flag byte from the header's byte 4 */
    *(uint8_t *)(nexthop_ret + 1) = (*(int8_t *)((uint8_t *)pkt + 4) < 0) ? 0xFF : 0x00;

    /* Copy header data to output packet (after 4-byte checksum header) */
    OS_$DATA_COPY(pkt, packet_buf + 4, (uint32_t)hdr_len);

    /* Return the header buffer */
    {
        uint32_t hdr_va = (uint32_t)(uintptr_t)pkt;
        NETBUF_$RTN_HDR(&hdr_va);
    }

    /* Check if there's additional data in the packet chain */
    if (rcv.data_pages[0] == 0) {
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
        PKT_$DAT_COPY(rcv.data_pages, copy_len, packet_buf + hdr_len + 4);

        /* Release the packet data buffers */
        PKT_$DUMP_DATA(rcv.data_pages, (int16_t)data_len);
    }

    /* Set total output length: checksum(4) + header + copied data */
    *length_ret = copy_len + hdr_len + 4;

    /* Compute checksum if enabled */
    checksum = 0x0DEC0DED;  /* Magic initial value */

    if (ROUTE_$USER_CHECKSUM < 0) {
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
