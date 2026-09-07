/*
 * ROUTE_$SEND_USER_PORT - Send packet to user routing port
 *
 * This function sends a packet to a user routing port for delivery.
 * It validates the packet size, copies the data to network buffers,
 * queues it to the socket, and updates statistics.
 *
 * Original address: 0x00E87C34
 * Size: 304 bytes
 */

#include "route/route_internal.h"
#include "sock/sock.h"
#include "net_io/net_io.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "misc/crash_system.h"
#include "arch/arch.h"

/*
 * ROUTE_$PACKET_SEQ (0xE88226) is declared in route/route_internal.h,
 * SOCK_$EVENT_COUNTERS (0xE28DB4) in sock/sock.h and
 * status_$network_data_length_too_large in network/network.h.
 */

/* Maximum packet length */
#define ROUTE_$MAX_SEND_LENGTH      0x400   /* 1024 bytes */

/* Status codes */
#define status_$route_queue_full                0x2B0002

/*
 * ROUTE_$SEND_USER_PORT - Queue packet to user routing port
 *
 * Sends a packet through a user routing port.  The header and the payload
 * pages are copied into fresh network buffers by NET_IO_$COPY_PACKET and the
 * resulting record is queued on the port's socket.
 *
 * Parameter offsets read off the prologue (link.w A6,-0x5c):
 *   A6+0x08  socket_ptr    movea.l (0x8,A6),A4      var word
 *   A6+0x0c  src_addr      never read
 *   A6+0x10  hdr_va        move.l (0x10,A6),D2, and its ADDRESS is handed to
 *                          NET_IO_$COPY_PACKET as the header source
 *                          (pea (0x10,A6) at 0x00E87CBC)
 *   A6+0x14  hdr_len       move.w (0x14,A6),D3w
 *   A6+0x16  src_pages     move.l (0x16,A6),D6, dereferenced at 0x00E87CD4
 *   A6+0x1a  src_data_va   move.l (0x1a,A6),-(SP) at 0x00E87CB6
 *   A6+0x1e  data_len      move.w (0x1e,A6),D4w
 *   A6+0x20  extra_ptr     never read
 *   A6+0x24  seq_ret       movea.l D7,A0 / clr.w (A0)
 *   A6+0x28  status_ret    movea.l (0x28,A6),A3
 *
 * @param socket_ptr    Pointer to the port's socket number
 * @param src_addr      Source address info (accepted, never used)
 * @param hdr_va        Header source VA; also becomes pkt_info.hdr
 * @param hdr_len       Header length
 * @param src_pages     Source payload page array (uint32_t[4])
 * @param src_data_va   Source payload VA, or 0 when src_pages is used
 * @param data_len      Payload byte count
 * @param extra_ptr     Extra protocol info (accepted, never used)
 * @param seq_ret       Output: packet sequence number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E87C34
 */
void ROUTE_$SEND_USER_PORT(uint16_t *socket_ptr, uint32_t src_addr, uint32_t hdr_va,
                           uint16_t hdr_len, uint32_t *src_pages,
                           uint32_t src_data_va, uint16_t data_len,
                           void *extra_ptr, uint16_t *seq_ret,
                           status_$t *status_ret)
{
    int16_t                 port_index;
    route_$port_stats_t    *driver_stats;   /* A2, port +0x44 */
    uint32_t                copy_hdr_va;    /* A6-0x58: NET_IO_$COPY_PACKET's
                                             *          new header buffer */
    uint32_t                copy_pages[4];  /* A6-0x50: its new payload pages */
    sock_$pkt_info_t        pkt_info;       /* A6-0x40 */
    int8_t                  put_result;
    uint8_t                 queue_count;    /* sock +0x15 */

    (void)src_addr;     /* 0x0c: consumed by the ABI, never read */
    (void)extra_ptr;    /* 0x20: consumed by the ABI, never read */

    /* 0x00E87C5E: clr.w (A0) */
    *seq_ret = 0;

    /* 0x00E87C62: cmpi.w #0x400,D4w / bls */
    if (data_len > ROUTE_$MAX_SEND_LENGTH) {
        *status_ret = status_$network_data_length_too_large;
        return;
    }

    /* 0x00E87C72: ROUTE_$FIND_PORT(2, *socket_ptr) */
    port_index = ROUTE_$FIND_PORT(2, (uint32_t)*socket_ptr);
    if (port_index == -1) {
        CRASH_SYSTEM(&ROUTE_$UNKNOWN_PORT_STATUS);
    }

    /*
     * 0x00E87CA4: movea.l (0x44,A0,D0*0x1),A2 - the statistics block is
     * reached as a 32-bit target address held in the port record.
     */
    driver_stats = (route_$port_stats_t *)
                   ARCH_VA_TO_PTR(ROUTE_$PORT_ARRAY[port_index].driver_stats);

    /* 0x00E87CA8 - 0x00E87CC0 */
    NET_IO_$COPY_PACKET(&hdr_va, hdr_len, src_data_va, src_pages, data_len,
                        &copy_hdr_va, copy_pages, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E87CD0 - 0x00E87CE2: build the SOCK_$PUT record.
     *
     * Note that pkt_info.hdr is set from the CALLER's hdr_va, not from the
     * copy NET_IO_$COPY_PACKET just made in copy_hdr_va, and only the first
     * of the four copied payload pages is recorded.  Preserved as-is; see
     * the TODO below.
     */
    pkt_info.hdr      = ARCH_VA_TO_PTR(hdr_va);         /* +0x00 */
    pkt_info.data_pages[0] = *src_pages;                /* +0x30 */
    pkt_info.hdr_len  = hdr_len;                        /* +0x2c */
    pkt_info.data_len = data_len;                       /* +0x2a */
    pkt_info.n_hops   = 0;                              /* +0x12 */

    /*
     * 0x00E87CE6: SOCK_$PUT(*socket_ptr, &pkt_info, 0, 2, *socket_ptr).
     * The cast restates the "pea (-0x40,A6)" the machine code pushes:
     * SOCK_$PUT_INT_INT reads the record through that same pointer.
     */
    put_result = SOCK_$PUT(*socket_ptr, (void **)&pkt_info, 0, 2, *socket_ptr);

    if (put_result < 0) {
        /* 0x00E87D00: success - packet queued */
        *seq_ret = ROUTE_$PACKET_SEQ;

        /*
         * Update statistics based on socket queue depth.
         *
         * The original indexes the socket pointer table from 0xE28DB4
         * with an offset of -4, i.e. entry (socket - 1) of
         * SOCK_$EVENT_COUNTERS (= entry [socket] of the table based at
         * 0xE28DB0, whose slot 0 is the spinlock):
         *   00e87d08    movea.l #0xe28db4,A1
         *   00e87d0e    lsl.w #0x2,D0w
         *   00e87d10    lea (0x0,A1,D0w*0x1),A1
         *   00e87d16    movea.l (-0x4,A1),A3
         */
        queue_count = ((const sock_$sock_t *)
                       SOCK_$EVENT_COUNTERS[*socket_ptr - 1])->queue_count;

        if (queue_count > 0x20) {
            /* 0x00E87D24: addq.l #0x1,(0x2,A2) */
            driver_stats->deep_queue_puts += 1;
        } else {
            /* 0x00E87D30: addq.l #0x1,(0xa,A2,D1*0x1) */
            driver_stats->queue_depth[queue_count] += 1;
        }
    } else {
        /* 0x00E87D36: addq.l #0x1,(0x6,A2) */
        driver_stats->failed_puts += 1;

        /* 0x00E87D3A / 0x00E87D4E: clean up the copies */
        NETBUF_$RTN_HDR(&copy_hdr_va);
        PKT_$DUMP_DATA(copy_pages, (int16_t)data_len);

        *status_ret = status_$route_queue_full;
    }
}
