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
#include "arch/arch.h"

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
 * @param port_info     route_$short_port_t; only its port_type (+0x06) and
 *                      socket (+0x08) are read (0x00E87A6C / 0x00E87A76).
 *                      Declared void * in route/route.h because the syscall
 *                      table hands it over untyped.
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
    const route_$short_port_t *pi;
    int16_t port_index;                 /* D0w */
    uint16_t port_type;
    int16_t socket;
    route_$port_t *port;
    sock_$pkt_info_t rcv;     /* A6-0x50: SOCK_$GET's packet record */
    route_$internet_hdr_t *pkt;  /* A2: the dequeued header buffer */
    rip_$nexthop_t next_hop;  /* A6-0x60: RIP_$FIND_NEXTHOP's answer */
    int16_t nexthop_port;     /* A6-0x82: RIP_$FIND_NEXTHOP's port */
    /*
     * A6-0x10: the 12-byte rip_$dest_addr_t the lookup is asked about.  Only
     * two of its four fields are ever written (see below), which is why it is
     * a record here and not a longword array - the writes land at +0x00 and
     * at the UNALIGNED +0x06, and +0x04 and +0x0A stay whatever the frame
     * held.
     */
    rip_$dest_addr_t dest_addr;
    int8_t sock_result;
    uint16_t hdr_len;         /* D2: header data length */
    uint16_t data_len;        /* D3: additional data length */
    uint32_t checksum;        /* A6-0x74 */
    int32_t copy_len;         /* D4 */
    int16_t i;

    /*
     * 0x00E87A6C-0x00E87A80.  ROUTE_$FIND_PORT's first argument is the port
     * TYPE word at port_info+0x06 (it compares it against port+0x2E at
     * 0x00E15B1E) and its second is the sign-extended socket word at +0x08
     * ("move.w (0x8,A2),D0w / ext.l D0").
     */
    pi = (const route_$short_port_t *)port_info;
    port_type = pi->port_type;
    socket = (int16_t)pi->socket;

    port_index = ROUTE_$FIND_PORT(port_type, (int32_t)socket);
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
    pkt = (route_$internet_hdr_t *)ARCH_VA_TO_PTR(rcv.hdr);
    hdr_len = pkt->hdr_len;
    data_len = pkt->data_len;

    /*
     * 0x00E87AEA - 0x00E87B02: destination for the nexthop lookup.
     *
     *   0x00E87AEA  move.l (0x2e,A0),(-0x10,A6)   pkt+0x2E = idp.dest_network
     *                                             -> dest_addr.network (+0x00)
     *   0x00E87AF0  move.l #0xffffff,D0
     *   0x00E87AF6  and.l (0x34,A0),D0            pkt+0x34 = the low four
     *                                             bytes of idp.dest_host
     *   0x00E87AFA  andi.l #-0x100000,(-0xa,A6)   A6-0x0A = dest_addr+0x06
     *   0x00E87B02  or.l D0,(-0xa,A6)             = dest_addr.host_lo
     *
     * A6-0x0C (dest_addr.host_hi, the top two bytes of the six-byte host) and
     * A6-0x06 (dest_addr.socket) are never written, and the read-modify-write
     * at A6-0x0A keeps whatever the frame held in bits 31..20; only bits 19..0
     * are cleared before the OR, so bits 23..20 of the packet's host address
     * are OR-ed into the leftovers rather than replacing them.  Reproduced as
     * found - the routine hands RIP_$FIND_NEXTHOP a partly uninitialised
     * record.
     */
    dest_addr.network = pkt->idp.dest_network;
    dest_addr.host_lo = (dest_addr.host_lo & 0xFFF00000u) |
                        ((((uint32_t)pkt->idp.dest_host[2] << 24) |
                          ((uint32_t)pkt->idp.dest_host[3] << 16) |
                          ((uint32_t)pkt->idp.dest_host[4] << 8) |
                          (uint32_t)pkt->idp.dest_host[5]) & 0x00FFFFFFu);

    /*
     * 0x00E87B06 - 0x00E87B1E.  The port goes to A6-0x82 and the 10-byte
     * next hop to A6-0x60; the data page vector (A6-0x20 = rcv.data_pages)
     * is a different buffer entirely.
     */
    RIP_$FIND_NEXTHOP(&dest_addr, false, &nexthop_port, &next_hop, status_ret);

    if (*status_ret != status_$ok) {
        /* Cleanup on failure (0x00E87B26 - 0x00E87B44) */
        uint32_t hdr_va = ARCH_PTR_TO_VA(pkt);   /* A6-0x80 (0x00E87B26) */

        NETBUF_$RTN_HDR(&hdr_va);
        PKT_$DUMP_DATA(rcv.data_pages, (int16_t)data_len);
        return;
    }

    /* 0x00E87B48 - 0x00E87B54: the node id is nexthop+6 masked to 20 bits */
    *nexthop_ret = next_hop.host_lo & 0xFFFFF;

    /*
     * 0x00E87B56 - 0x00E87B5C: "tst.b (0x4,A2) / smi D1b / move.b D1b,(0x4,A0)"
     * turns the sign of route_info's first byte into a Domain boolean in the
     * output record's byte at +0x04.
     */
    ((uint8_t *)nexthop_ret)[4] =
        ((int8_t)(pkt->route_info >> 24) < 0) ? 0xFF : 0x00;

    /* Copy header data to output packet (after 4-byte checksum header) */
    OS_$DATA_COPY(pkt, packet_buf + 4, (uint32_t)hdr_len);

    /* Return the header buffer */
    {
        uint32_t hdr_va = ARCH_PTR_TO_VA(pkt);   /* A6-0x80 (0x00E87B7A) */

        NETBUF_$RTN_HDR(&hdr_va);
    }

    /* Check if there's additional data in the packet chain */
    if (rcv.data_pages[0] == 0) {
        data_len = 0;
    }

    if (data_len == 0) {
        copy_len = 0;                       /* 0x00E87BD6 clr.w D4w */
    } else {
        /*
         * 0x00E87B96 - 0x00E87BA6.  D4 held the zero-extended header length;
         * "neg.l D4 / addi.l #0x7fc,D4" makes it the room left in the 0x7FC
         * buffer, and "cmp.l D0,D4 / ble" is a SIGNED minimum against the
         * zero-extended data length - a header longer than 0x7FC leaves a
         * negative room figure, which wins the compare and is passed on as a
         * negative length.
         */
        int32_t want = (int32_t)(uint32_t)data_len;      /* D0 */

        copy_len = (int32_t)ROUTE_$MAX_PACKET_DATA - (int32_t)(uint32_t)hdr_len;
        if (copy_len > want) {
            copy_len = want;
        }

        /* 0x00E87BAC - 0x00E87BC0: the word of D4 is what is passed */
        PKT_$DAT_COPY(rcv.data_pages, (int16_t)copy_len,
                      (char *)(packet_buf + hdr_len + 4));

        /* 0x00E87BC4 - 0x00E87BD2 */
        PKT_$DUMP_DATA(rcv.data_pages, (int16_t)data_len);
    }

    /*
     * 0x00E87BD8 - 0x00E87BDE: "add.w D4w,D2w / addq.w #0x4,D2w", so the sum
     * is formed in a word.
     */
    *length_ret = (int16_t)((uint16_t)((uint16_t)copy_len + hdr_len) + 4);

    /* Compute checksum if enabled */
    checksum = 0x0DEC0DED;  /* Magic initial value */

    if (ROUTE_$USER_CHECKSUM < 0) {
        /*
         * 0x00E87BEE - 0x00E87BF2: "subq.w #0x5,D2w" over the total length
         * already in D2, i.e. hdr_len + copy_len - 1, tested as a word.
         */
        int16_t checksum_len =
            (int16_t)((uint16_t)((uint16_t)copy_len + hdr_len) + 4 - 5);
        if (checksum_len >= 0) {
            for (i = 4; checksum_len >= 0; i++, checksum_len--) {
                checksum = (uint32_t)packet_buf[i] + checksum * 0x11;
            }
        }
    }

    /* Copy checksum to packet header */
    OS_$DATA_COPY(&checksum, packet_buf, 4);
}
