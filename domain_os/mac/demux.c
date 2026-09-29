/*
 * MAC_$DEMUX - the receive callback MAC_$OPEN registers with MAC_OS_$OPEN
 *
 * MAC_OS_$DEMUX has already resolved the frame to a channel; this routine
 * repacks the driver's mac_os_$rcv_pkt_t into the sock_$pkt_info_t the socket
 * layer expects and queues it on the channel's socket.
 *
 * Original address: 0x00E0BC4E, size 222 bytes (0x00E0BC4E-0x00E0BD2B).
 * Registered as the callback at 0x00E0B9A8 ("move.l #0xe0bc4e,(-0x8,A6)").
 *
 * There is no hardware here, so the body is portable (bead source-yoa1; it
 * used to sit under "#if defined(ARCH_M68K)" and hand SOCK_$PUT the caller's
 * record instead of the 0x40-byte descriptor the image builds).
 */

#include "mac/mac_internal.h"

/*
 * MAC_$DEMUX
 *
 * Parameters (0x08, 0x0C, 0x10, 0x14 off A6):
 *   pkt_info   - the driver's received-packet record
 *   port_num   - pointer to the port number, used to index ROUTE_$PORTP
 *   demux_flag - pointer to a Domain boolean; true sets SOCK_PKT_FLAG_DEMUX_BOOL
 *   status_ret - status return
 */
void MAC_$DEMUX(void *pkt_info, int16_t *port_num, int8_t *demux_flag,
                status_$t *status_ret)
{
    mac_os_$rcv_pkt_t  *pkt = (mac_os_$rcv_pkt_t *)pkt_info;
    sock_$pkt_info_t    desc;           /* A6-0x40 */
    route_$port_t      *route_port;
    mac_os_$channel_t  *chan;
    uint16_t            n_words;
    uint16_t            i;

    /* 0x00E0BC5E: clr.l (A0) */
    *status_ret = status_$ok;

    /*
     * 0x00E0BC62-0x00E0BC80.  The word at descriptor + 0x10 starts at 2 and
     * the two bsets work on the byte at + 0x11, the word's LOW half, so they
     * name word bits 0 and 2.
     */
    desc.flags = SOCK_PKT_FLAG_XNS;
    if (pkt->is_local < 0) {
        desc.flags |= SOCK_PKT_FLAG_LOCAL;
    }
    if (*demux_flag < 0) {
        desc.flags |= SOCK_PKT_FLAG_DEMUX_BOOL;
    }

    /*
     * 0x00E0BC82-0x00E0BC9C: the link address's word count, then that many
     * address words, copied one word at a time.  The count is not bounded,
     * exactly as in MAC_$RECEIVE's mirror-image loop.
     */
    n_words = pkt->link_addr.n_words;
    desc.n_hops = n_words;
    for (i = 0; i < n_words; i++) {
        desc.hops[i] = pkt->link_addr.addr[i];
    }

    /*
     * 0x00E0BC9E-0x00E0BCD0: the rest of the descriptor.  MAC_OS_$DEMUX left
     * the arrival time from TIME_$ABS_CLOCK in the record at +0x2A/+0x2E
     * (0x00E0B890), and this path carries those two halves in the socket
     * record's src_addr/src_port slots; the frame type goes in dst_addr.
     *   move.l (0x2a,A2),(-0x3c,A6)   -> desc + 0x04
     *   move.w (0x2e,A2),(-0x38,A6)   -> desc + 0x08
     *   move.l (0x30,A2),(-0x34,A6)   -> desc + 0x0C
     *   move.l (0x20,A2),(-0x40,A6)   -> desc + 0x00
     *   move.w (0x1e,A2),(-0x14,A6)   -> desc + 0x2C, the LOW word of body_len
     *   move.w (0x3a,A2),(-0x16,A6)   -> desc + 0x2A, the LOW word of data_len
     *   lea (0x3c,A2),A0 / four move.l (A0)+,(A1)+ -> desc + 0x30
     * The two holes at desc + 0x0A and desc + 0x2E are left as the stack
     * found them.
     */
    desc.hdr      = pkt->body;
    desc.src_addr = pkt->time_high;
    desc.src_port = pkt->time_low;
    desc.dst_addr = pkt->frame_type;
    desc.hdr_len  = (uint16_t)(pkt->body_len & 0xFFFF);
    desc.data_len = (uint16_t)(pkt->data_len & 0xFFFF);
    for (i = 0; i < 4; i++) {
        desc.data_pages[i] = pkt->data_pa[i];
    }

    /* 0x00E0BCD2-0x00E0BCE6: A2 is reloaded with the resolved channel */
    chan       = (mac_os_$channel_t *)ARCH_VA_TO_PTR(pkt->channel);
    route_port = ROUTE_$WIRED_DATA.portp[*port_num];

    /* 0x00E0BCE8-0x00E0BCF8 */
    if (chan->socket == MAC_NO_SOCKET) {
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /*
     * 0x00E0BCFA-0x00E0BD20: SOCK_$PUT(chan->socket, &desc, false,
     *                                  route_port->port_type, route_port->socket)
     * and a non-negative answer means the packet was not queued.
     */
    if (SOCK_$PUT(chan->socket, &desc, 0,
                  route_port->port_type, route_port->socket) >= 0) {
        *status_ret = status_$mac_failed_to_put_packet_into_socket;
    }
}
