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

/*
 * ROUTE_$PACKET_SEQ - Packet sequence number
 *
 * Returned to caller to identify the packet.
 *
 * Original address: 0xE88226
 */
#if defined(M68K)
    #define ROUTE_$PACKET_SEQ       (*(uint16_t *)0xE88226)
#else
    extern uint16_t ROUTE_$PACKET_SEQ;
#endif

/*
 * Socket event counter array reference
 * Original address: 0xE28DB4
 */
#if defined(M68K)
    #define SOCK_$EC_ARRAY          ((void **)0xE28DB4)
#else
    extern void *SOCK_$EC_ARRAY[];
#endif

/* Maximum packet length */
#define ROUTE_$MAX_SEND_LENGTH      0x400   /* 1024 bytes */

/* Status codes */
#define status_$network_data_length_too_large   0x11001C
#define status_$route_queue_full                0x2B0002

/* Crash error reference */
extern uint32_t OS_Internet_unknown_network_port_err;

/*
 * ROUTE_$SEND_USER_PORT - Queue packet to user routing port
 *
 * Sends a packet through a user routing port. The packet is copied
 * to network buffers and queued to the socket for delivery.
 *
 * @param socket_ptr    Pointer to socket number
 * @param src_addr      Source address info
 * @param dest_addr     Destination address pointer
 * @param header_len    Header length
 * @param flags1        Protocol flags 1
 * @param flags2        Protocol flags 2
 * @param data_ptr      Packet data pointer
 * @param data_len      Packet data length
 * @param extra_ptr     Extra protocol info pointer
 * @param seq_ret       Output: packet sequence number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E87C34
 */
void ROUTE_$SEND_USER_PORT(uint16_t *socket_ptr, uint32_t src_addr, void *dest_addr,
                           uint16_t header_len, uint16_t flags1, uint16_t flags2,
                           void *data_ptr, uint16_t data_len, void *extra_ptr,
                           uint16_t *seq_ret, status_$t *status_ret)
{
    int16_t port_index;
    uint32_t *driver_stats;
    void *hdr_buf[8];           /* Header buffer chain */
    void *data_buf[16];         /* Data buffer chain */
    uint32_t pkt_info[4];       /* Packet info structure */
    int8_t put_result;
    uint8_t sock_type;

    /* Initialize sequence number return to 0 */
    *seq_ret = 0;

    /* Validate packet length */
    if (data_len > ROUTE_$MAX_SEND_LENGTH) {
        *status_ret = status_$network_data_length_too_large;
        return;
    }

    /* Find the port - using network type 2 (routing) */
    port_index = ROUTE_$FIND_PORT(2, (uint32_t)*socket_ptr);
    if (port_index == -1) {
        CRASH_SYSTEM(&OS_Internet_unknown_network_port_err);
    }

    /*
     * Get the driver statistics pointer from the port structure.
     * Located at offset 0x44 in the port structure.
     */
    driver_stats = (uint32_t *)ROUTE_$PORT_ARRAY[port_index]._unknown0[0x40];

    /* Copy packet data to network buffers */
    NET_IO_$COPY_PACKET(&dest_addr, header_len, data_ptr,
                        (flags1 << 16) | flags2, data_len,
                        hdr_buf, data_buf, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Build the packet info structure for SOCK_$PUT:
     *   +0x00: Source address
     *   +0x10: Extra protocol info (from extra_ptr)
     *   +0x14: Header length
     *   +0x16: Data length
     *   +0x2E: Reserved (0)
     */
    pkt_info[0] = src_addr;
    /* Copy extra info from data_ptr (which points to protocol info) */
    {
        uint32_t *extra = (uint32_t *)data_ptr;
        *(uint32_t *)((uint8_t *)pkt_info + 0x10) = *extra;
    }
    *(uint16_t *)((uint8_t *)pkt_info + 0x14) = header_len;
    *(uint16_t *)((uint8_t *)pkt_info + 0x16) = data_len;
    *(uint16_t *)((uint8_t *)pkt_info + 0x2E) = 0;

    /* Queue the packet to the socket */
    put_result = SOCK_$PUT(*socket_ptr, pkt_info, 0, 2, *socket_ptr);

    if (put_result < 0) {
        /* Success - packet queued */
        *seq_ret = ROUTE_$PACKET_SEQ;

        /*
         * Update statistics based on socket type.
         * Socket event counter has type at offset 0x15.
         */
        void *sock_ec = SOCK_$EC_ARRAY[*socket_ptr];
        sock_type = *(uint8_t *)((uint8_t *)sock_ec + 0x15);

        if (sock_type > 0x20) {
            /* General counter at offset 2 */
            driver_stats[0] = driver_stats[0] + 1;  /* Actually at +2 bytes */
        } else {
            /* Per-type counter at offset 10 + type*4 */
            uint32_t *type_counter = (uint32_t *)((uint8_t *)driver_stats + 10 + sock_type * 4);
            *type_counter = *type_counter + 1;
        }
    } else {
        /* Failure - queue full */
        /* Increment error counter at offset 6 */
        *(uint32_t *)((uint8_t *)driver_stats + 6) += 1;

        /* Clean up buffers */
        NETBUF_$RTN_HDR(hdr_buf);
        PKT_$DUMP_DATA(data_buf, data_len);

        *status_ret = status_$route_queue_full;
    }
}
