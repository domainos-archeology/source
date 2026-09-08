/*
 * ring/receive_packet.c - ring_$receive_packet (0x00E7649E)
 *
 * Dispatches one validated receive to its destination:
 *   - packets whose header does not carry the "ring service" flag go straight
 *     to NET_IO_$PUT_IN_SOCK,
 *   - otherwise the header's route_info is looked up in the unit's packet-type
 *     table to find the owning channel, and the packet is handed either to
 *     MAC_OS_$DEMUX (channels opened by the OS) or to SOCK_$PUT,
 *   - anything that cannot be delivered has its buffers returned.
 *
 * Called only from RING_$RCV_FROM_UNIT_PRIV (0x00E76264), which passes the
 * ADDRESSES of four of its own locals; the callee both reads and writes them.
 */

#include "ring/ring_internal.h"
#include "mac_os/mac_os.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
/* NETBUF_$RTN_HDR: netbuf/netbuf.h, TIME_$ABS_CLOCK: time/time.h
 * (both via ring/ring_internal.h) */

/*
 * The 0x40-byte record ring_$receive_packet builds for MAC_OS_$DEMUX is
 * mac_os_$rcv_pkt_t (mac_os/mac_os.h); the ring driver fills net_type
 * (0x00E76528), src_id (0x00E76532), is_local (0x00E7651E), body_len and
 * body, clears +0x24 (0x00E7656A), and sets frame_type, data_len and data_pa.
 */

/*
 * ring_$discard_packet (0x00E76470) - give the header buffer back to NETBUF
 * and dump the data pages.
 *
 * This is a nested Pascal procedure of ring_$receive_packet: it reads the
 * parent's fifth parameter (data_len_p, at parent A6+0x16) through the static
 * link ("movea.l (A6),A2" at 0x00E76476), which is why data_len_p is threaded
 * in here explicitly.  Its own third argument is pushed by every caller but
 * never used by the body.
 */
static void ring_$discard_packet(ring_$pkt_hdr_t *hdr, uint32_t *data_pa_p,
                                 int16_t unused_len, const int16_t *data_len_p)
{
    (void)unused_len;   /* 0x00E76470: pushed at (0x10,A6), never referenced */

    /* 0x00E76478: NETBUF_$RTN_HDR(&hdr) - the local copy is passed by address */
    NETBUF_$RTN_HDR((uint32_t *)&hdr);

    /* 0x00E76484: PKT_$DUMP_DATA(data_pa_p, *data_len_p) */
    PKT_$DUMP_DATA(data_pa_p, *data_len_p);
}

/*
 * ring_$receive_packet - dispatch one received packet
 *
 * Original address: 0x00E7649E
 *
 * @param unit          Unit number
 * @param hdr_p         Address of the received header pointer
 * @param data_pa_p     Address of the data buffer DMA address (0 if none)
 * @param hdr_len_p     Address of the received header byte count
 * @param data_len_p    Address of the received data byte count
 *
 * @return The caller reserves a word result slot but the routine never stores
 *         into it, so the value is meaningless; RING_$RCV discards it.
 */
int16_t ring_$receive_packet(uint16_t unit, ring_$pkt_hdr_t **hdr_p,
                             uint32_t *data_pa_p, int16_t *hdr_len_p,
                             int16_t *data_len_p)
{
    ring_unit_t         *unit_data;         /* A2 */
    ring_$pkt_hdr_t     *hdr;               /* A3 after 0x00E764CC */
    mac_os_$rcv_pkt_t  mac_rec;           /* A6-0x50 */
    sock_$pkt_info_t     sock_rec;          /* A6-0x90 */
    status_$t            status;            /* A6-0x98 */
    boolean              demux_flag;        /* A6-0xA6 */
    uint16_t             flags7;            /* A6-0xA0: written, never read */
    uint16_t             socket_id;         /* A6-0xA4 */
    int16_t              pkt_type_idx;
    int16_t              chan;
    boolean              is_fragment;       /* D3 */
    int8_t               queued;

    /* 0x00E764B4: A2 = &RING_$CTL.units[unit] */
    unit_data = &RING_$CTL.units[unit];

    /* 0x00E764BE */
    hdr = *hdr_p;

    /*
     * 0x00E764C0: bit 0 of hdr->flags selects the ring's own service
     * demultiplexing; without it the packet belongs to the generic network
     * socket layer.
     */
    if ((hdr->flags & 0x01) == 0) {
        /*
         * 0x00E7661E: NET_IO_$PUT_IN_SOCK(0, unit, hdr_p, data_pa_p,
         *                                 *hdr_len_p, *data_len_p).
         * No stack adjustment follows the call - "unlk A6" restores SP.
         * The callee takes the *address* of a 32-bit VA cell; here that cell
         * is the caller's ring_$pkt_hdr_t * , so the cast just restates the
         * longword the "pea"/"move.l" pushes.
         */
        NET_IO_$PUT_IN_SOCK(0, unit, (uint32_t *)hdr_p, data_pa_p,
                            (uint16_t)*hdr_len_p, (uint16_t)*data_len_p);
        return 0;
    }

    /*
     * 0x00E764CA: look the packet type up in the unit's table.  The table is
     * a 1-based Pascal array, so a hit is reported as an index >= 1.
     */
    pkt_type_idx = ring_$find_pkt_type(hdr->route_info, unit_data->pkt_types,
                                       unit_data->pkt_type_cnt);
    if (pkt_type_idx == 0) {
        goto discard;                               /* 0x00E7660C */
    }

    /* 0x00E764E8-0x00E764F6 */
    chan = unit_data->pkt_types[pkt_type_idx - 1].channel;

    /* 0x00E764FC: the owning channel must still be open. */
    if (unit_data->channels[chan - 1].flags >= 0) {
        goto discard;                               /* 0x00E7660C */
    }

    /* 0x00E76504: kept in a local that nothing reads back. */
    flags7 = (uint16_t)hdr->flags7;
    (void)flags7;

    /* 0x00E7650E: bit 3 of hdr->flags7 */
    is_fragment = ((hdr->flags7 & 0x08) != 0) ? true : false;

    /* 0x00E76514 */
    if (unit_data->channels[chan - 1].socket_id == RING_OS_SOCKET_ID) {
        /*
         * OS-owned channel: build the MAC_OS demultiplex record.
         */
        /* 0x00E7651E: smi on hdr->flags - bit 7 */
        mac_rec.is_local = ((hdr->flags & 0x80) != 0) ? true : false;

        mac_rec.net_type = 2;                       /* 0x00E76528 */
        mac_rec.src_id = hdr->src_id;               /* 0x00E76532: 2 words */
        mac_rec.frame_type = hdr->route_info;       /* 0x00E76542 */
        mac_rec.data_pa[0] = *data_pa_p;               /* 0x00E7654A */
        mac_rec.body_len = (int32_t)(uint32_t)(uint16_t)*hdr_len_p
                           - (int32_t)RING_HDR_SIZE;    /* 0x00E76556 */
        mac_rec.body = (uint32_t)(uintptr_t)*hdr_p + RING_HDR_SIZE; /* 0x00E76564 */
        mac_rec._r24 = 0;                           /* 0x00E7656A */
        mac_rec.data_len = (uint32_t)(uint16_t)*data_len_p; /* 0x00E76576 */

        demux_flag = is_fragment;                   /* 0x00E7657A */

        /* 0x00E7657E-0x00E7659C */
        MAC_OS_$DEMUX(&mac_rec, &RING_$CTL.port_array[unit], &demux_flag,
                      &status);

        if (status == status_$ok) {                 /* 0x00E765A0 */
            return 0;                               /* 0x00E76638 */
        }

        /* 0x00E765A8: fall into the discard helper with the header pointer. */
        ring_$discard_packet(*hdr_p, data_pa_p, *data_len_p, data_len_p);
        return 0;
    }

    /*
     * 0x00E765B8: ordinary channel - queue the packet on its socket.
     */
    socket_id = (uint16_t)unit_data->channels[chan - 1].socket_id;
    /* sock_$pkt_info_t.hdr is a target VA, not a C pointer. */
    sock_rec.hdr = ARCH_PTR_TO_VA(hdr);             /* 0x00E765BE */
    sock_rec.data_pages[0] = *data_pa_p;            /* 0x00E765C2 */
    sock_rec.hdr_len = (uint16_t)*hdr_len_p;        /* 0x00E765C8 */
    sock_rec.data_len = (uint16_t)*data_len_p;      /* 0x00E765D0 */

    /*
     * 0x00E765D8: "clr.l (-0x80,A6)" clears both the flags word and the hop
     * count; only the flags word is then set.
     */
    sock_rec.flags = 0;
    sock_rec.n_hops = 0;
    if (is_fragment < 0) {
        sock_rec.flags = 4;                         /* 0x00E765E0 */
    }

    /*
     * 0x00E765E6: the ring stamps the arrival time into the six bytes at
     * +0x04, the slot SOCK_$GET reports as src_addr/src_port.
     */
    TIME_$ABS_CLOCK((clock_t *)&sock_rec.src_addr);

    /* 0x00E765F2 */
    queued = SOCK_$PUT(socket_id, &sock_rec, 0, 0, unit);
    if (queued < 0) {
        return 0;                                   /* 0x00E76638 */
    }

discard:
    /* 0x00E7660C */
    ring_$discard_packet(hdr, data_pa_p, *data_len_p, data_len_p);
    return 0;
}
