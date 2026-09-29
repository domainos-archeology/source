/*
 * MAC_$OPEN - Open a MAC channel
 *
 * Validates the caller's port and packet-type ranges, allocates a user socket
 * for the channel, builds the mac_os_$open_params_t MAC_OS_$OPEN wants (with
 * MAC_$DEMUX as the receive callback), and publishes the channel to the
 * caller.
 *
 * Original address: 0x00E0B8BE, size 430 bytes (0x00E0B8BE-0x00E0BA6B).
 * A5 = 0x00E22990 (MAC_OS_$DATA).
 */

#include "mac/mac_internal.h"

void MAC_$OPEN(int16_t *port_num, mac_$open_params_t *params,
               status_$t *status_ret)
{
    int16_t                  port;
    int16_t                  num_types;
    int16_t                  i;
    uint16_t                 sock_num;
    uint8_t                 *sock_ptr;
    mac_os_$open_params_t    os_params;   /* A6-0x58 */
    status_$t                os_status;   /* A6-0x5c */
    uint16_t                 channel_num;
    uint32_t                 os_mtu;

    /* 0x00E0B8D8: clr.l (A0) */
    *status_ret = status_$ok;

    /* 0x00E0B8DC-0x00E0B8EE: 0 <= port <= 7 */
    port = *port_num;
    if (port < 0 || port > 7) {
        *status_ret = status_$mac_invalid_port;
        return;
    }

    /*
     * 0x00E0B8F2-0x00E0B90C:
     *   moveq  #0x5c,D1 / movea.l #0xe2e0a0,A3 / mulu.w D0w,D1
     *   moveq  #0x3,D3
     *   move.w (0x2c,A3,D1w*0x1),D2w
     *   btst.l D2,D3
     * 0xE2E0A0 with a 0x5C stride is ROUTE_$PORT_ARRAY and +0x2C is
     * route_$port_t.active.  btst on a data register takes the bit number
     * modulo 32.
     */
    {
        uint16_t port_type = ROUTE_$PORT_ARRAY[port].active;

        if ((1u << (port_type & 0x1F)) & 3) {
            *status_ret = status_$internet_network_port_not_open;
            return;
        }
    }

    /* 0x00E0B910-0x00E0B922: 1 <= num_packet_types <= 10 */
    num_types = params->num_packet_types;
    if (num_types < 1 || num_types > MAC_MAX_PACKET_TYPES) {
        *status_ret = status_$mac_invalid_packet_type_count;
        return;
    }

    /* 0x00E0B926-0x00E0B948: every range must have min <= max */
    for (i = 0; i < num_types; i++) {
        if (params->u.packet_types[i].min_type >
            params->u.packet_types[i].max_type) {
            *status_ret = status_$mac_invalid_packet_type;
            return;
        }
    }

    /* 0x00E0B94C-0x00E0B95A */
    if (params->socket_count == 0) {
        *status_ret = status_$mac_no_socket_allocated;
        return;
    }

    /*
     * 0x00E0B95E-0x00E0B988.  The socket_count word is pushed three times -
     * "move.w (0x52,A2),-(SP) / move.w (SP),-(SP) / move.w (SP),-(SP)" - so
     * arguments 2, 3 and 4 all carry it.  SOCK_$ALLOCATE_USER answers with a
     * Domain boolean; a non-negative answer means it failed.
     */
    if (SOCK_$ALLOCATE_USER(&sock_num, params->socket_count,
                            params->socket_count, params->socket_count,
                            0x400) >= 0) {
        *status_ret = status_$mac_no_os_sockets_available;
        return;
    }

    /*
     * 0x00E0B98E-0x00E0B9A2: 0xE28DB4 indexed by sock_num*4 less 4 is slot
     * sock_num of the socket pointer table, i.e. SOCK_$DATA.socket_ptr[sock_num].
     * The bclr is on the byte at descriptor offset 0x16.
     */
    sock_ptr = (uint8_t *)SOCK_$DATA.socket_ptr[sock_num];
    sock_ptr[0x16] &= 0x7F;

    /*
     * 0x00E0B9A8-0x00E0B9CE: build the LOCAL mac_os_$open_params_t at A6-0x58.
     *   move.l #0xe0bc4e,(-0x8,A6)   -> base + 0x50, the callback = MAC_$DEMUX
     *   move.w (0x50,A2),(-0x4,A6)   -> base + 0x54, the entry count
     *   move.l (A3)+,(-0x58,A0) / move.l (A3)+,(-0x54,A0) / dbf
     *                                -> base + 0x00.., the range pairs
     * The caller's record is NOT handed on: its 0x50/0x52/0x54 are the count,
     * the socket count and the flags byte, none of which MAC_OS_$OPEN reads
     * that way (bead source-0kuj).
     */
    os_params.callback      = (void *)MAC_$DEMUX;
    os_params.num_pkt_types = (uint16_t)params->num_packet_types;
    for (i = 0; i < num_types; i++) {
        os_params.u.pkt_types[i].range_low  = params->u.packet_types[i].min_type;
        os_params.u.pkt_types[i].range_high = params->u.packet_types[i].max_type;
    }

    /* 0x00E0B9D2-0x00E0B9EA */
    MAC_OS_$OPEN(port_num, &os_params, &os_status);
    *status_ret = os_status;

    if (os_status != status_$ok) {
        /* 0x00E0B9EC-0x00E0B9F6 */
        SOCK_$CLOSE(sock_num);
        return;
    }

    /*
     * 0x00E0B9FA: move.w (-0x54,A6),D2w - the channel MAC_OS_$OPEN wrote over
     * the local record's first entry (base + 0x04), not anything in the
     * caller's record.
     */
    channel_num = os_params.u.result.channel;
    os_mtu      = os_params.u.result.mtu;    /* base + 0x00, read at 0x00E0BA58 */

    /* 0x00E0B9F8-0x00E0BA08: PROC2_$SET_CLEANUP(0x0D) */
    PROC2_$SET_CLEANUP(0x0D);

    /* 0x00E0BA0A */
    ML_$EXCLUSION_START(&MAC_OS_$EXCLUSION);

    /* 0x00E0BA16-0x00E0BA26: chan->socket = sock_num */
    MAC_OS_$CHANNEL_TABLE[channel_num].socket = sock_num;

    /*
     * 0x00E0BA2A-0x00E0BA38:
     *   move.b (0x54,A2),D1b / lsr.b #0x7,D1b
     *   andi.b #-0x2,(0x7b2,A0) / or.b D1b,(0x7b2,A0)
     * A5+0x7B2 is channel offset 0x12, the HIGH byte of the flags word, so
     * bit 0 of that byte is word bit 8 - MAC_OS_CHANNEL_PROMISCUOUS.  The
     * read-modify-write is done on the whole word here so it does not depend
     * on byte order; the exclusion lock is held throughout.
     */
    {
        uint16_t flags   = MAC_OS_$CHANNEL_TABLE[channel_num].flags;
        uint16_t promisc = (uint16_t)((params->flags >> 7) & 1);

        flags &= (uint16_t)~MAC_OS_CHANNEL_PROMISCUOUS;
        flags |= (uint16_t)(promisc * MAC_OS_CHANNEL_PROMISCUOUS);
        MAC_OS_$CHANNEL_TABLE[channel_num].flags = flags;
    }

    /* 0x00E0BA3A */
    ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);

    /*
     * 0x00E0BA46-0x00E0BA60.  D6 still holds &table slot sock_num
     * ("movea.l D6,A0 / movea.l (-0x4,A0),A3"), i.e. the same descriptor the
     * bclr above used.  The three results then overwrite the head of the
     * caller's record.
     */
    sock_ptr = (uint8_t *)SOCK_$DATA.socket_ptr[sock_num];
    params->u.result.ec2_handle =
        ARCH_PTR_TO_VA(EC2_$REGISTER_EC1((ec_$eventcount_t *)sock_ptr, status_ret));
    params->u.result.mtu         = os_mtu;
    params->u.result.channel_num = channel_num;
}
