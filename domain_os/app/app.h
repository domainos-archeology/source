/*
 * APP - Application Protocol Module
 *
 * This module provides application-level network protocol handling,
 * building on top of the XNS IDP (Internet Datagram Protocol) layer.
 *
 * The APP module handles:
 * - Receiving and parsing network packets from sockets
 * - Demultiplexing incoming packets to appropriate handlers
 * - Opening standard application channels for network services
 *
 * APP is the application layer in the Apollo network stack:
 *   RING (physical) -> XNS_IDP (network) -> SOCK (transport) -> APP (application)
 */

#ifndef APP_H
#define APP_H

#include "base/base.h"

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

#define status_$network_buffer_queue_is_empty   0x00110006

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * app_$receive_rec_t - the 44-byte result record APP_$RECEIVE fills in
 *
 * APP_$RECEIVE (0x00E00800) takes the record in A2 and drives it from the
 * sock_$pkt_info_t SOCK_$GET writes at A6-0x40:
 *
 *   0x00  the application reply record.  For XNS ("standard") routing it is
 *         the received header + 0x1E ("moveq #0x1e,D0 / add.l (-0x40,A6),D0 /
 *         move.l D0,(A2)" at 0x00E008A8); for Domain internet routing it is
 *         the header + the rounded template length, or a copy APP_$RECEIVE
 *         makes when the packet does not fit (0x00E00940-0x00E0097A).
 *   0x04  the payload that follows that record - reply + 0x18 on the XNS
 *         path (0x00E008B0), header + hdr_size + 0x1E on the internet path
 *         (0x00E0093C).  This, not the reply pointer, is what callers hand
 *         to NETBUF_$RTN_HDR.
 *   0x08  the four payload page addresses, copied straight out of
 *         sock_$pkt_info_t.data_pages (0x00E0083C-0x00E0084A).  This is the
 *         vector callers pass to PKT_$DUMP_DATA.
 *   0x18  longword from the received header + 0x06 (0x00E008BC)
 *   0x1C  longword from the received header + 0x12 (0x00E008C2)
 *   0x20  source address, from sock_$pkt_info_t.src_addr (0x00E0087E)
 *   0x24  source port,    from sock_$pkt_info_t.src_port (0x00E00884)
 *   0x26  an UNALIGNED flags longword: the socket queue depth is shifted into
 *         it at 0x00E0086E-0x00E0087A, bit 7 of +0x27 carries a header flag
 *         (0x00E0089A / 0x00E0090C), and +0x28 is masked and or-ed as a word
 *         (0x00E008C8, 0x00E00994).
 */
typedef struct app_$receive_rec_t {
    void       *reply;              /* 0x00 */
    void       *data;               /* 0x04 */
    uint32_t    data_pages[4];      /* 0x08 */
    uint32_t    hdr_f06;            /* 0x18 */
    uint32_t    hdr_f12;            /* 0x1C */
    uint32_t    src_addr;           /* 0x20 */
    uint16_t    src_port;           /* 0x24 */
    uint16_t    flags_hi;           /* 0x26: the two halves of the UNALIGNED
                                     *       longword the original masks with
                                     *       "andi.l #-0x7f8001,(0x26,A2)" at
                                     *       0x00E0086E; kept as two words so
                                     *       the record needs no packing and
                                     *       its members stay addressable */
    uint16_t    flags_lo;           /* 0x28 */
    uint8_t     _pad_2a[2];         /* 0x2A */
} app_$receive_rec_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(app_$receive_rec_t, data)       == 0x04, "app_rcv.data");
_Static_assert(offsetof(app_$receive_rec_t, data_pages) == 0x08, "app_rcv.data_pages");
_Static_assert(offsetof(app_$receive_rec_t, hdr_f06)    == 0x18, "app_rcv.hdr_f06");
_Static_assert(offsetof(app_$receive_rec_t, hdr_f12)    == 0x1C, "app_rcv.hdr_f12");
_Static_assert(offsetof(app_$receive_rec_t, src_addr)   == 0x20, "app_rcv.src_addr");
_Static_assert(offsetof(app_$receive_rec_t, src_port)   == 0x24, "app_rcv.src_port");
_Static_assert(offsetof(app_$receive_rec_t, flags_hi)   == 0x26, "app_rcv.flags_hi");
_Static_assert(offsetof(app_$receive_rec_t, flags_lo)   == 0x28, "app_rcv.flags_lo");
_Static_assert(sizeof(app_$receive_rec_t) == 0x2C, "app_$receive_rec_t must be 44 bytes");
#endif

/*
 * APP_$RECEIVE - Receive a packet on a socket
 *
 * Receives the next available packet from a socket and parses the
 * network headers. The result structure is filled with pointers to
 * the header and data areas, along with addressing information.
 *
 * For local network packets (type 1), the source/dest are on the same node.
 * For remote network packets (type 2), full network addresses are extracted.
 *
 * If the packet is too large to process inline (>952 bytes), it is
 * copied to a temporary buffer with an exclusion lock held.
 *
 * Parameters:
 *   sock_num   - Socket number to receive from
 *   result     - Pointer to receive result structure (44 bytes)
 *   status_ret - Output: status code
 *
 * Status codes:
 *   status_$ok - Packet received successfully
 *   status_$network_buffer_queue_is_empty - No packets available
 *
 * Original address: 0x00E00800
 */
void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret);

/*
 * APP_$DEMUX - Demultiplex received packet
 *
 * Routes a received packet to the appropriate handler based on the
 * socket type. This function is called by the XNS IDP layer when
 * a packet arrives for the standard application protocol.
 *
 * If the packet cannot be delivered to the target socket (e.g., socket
 * buffer full), it attempts to queue it to the overflow socket instead.
 *
 * After processing, the header and data buffers are returned to the pool.
 *
 * Parameters:
 *   pkt_info   - Pointer to packet info structure from XNS_IDP
 *   ec_ptr1    - Event count pointer 1
 *   ec_ptr2    - Event count pointer 2
 *   flags      - Processing flags
 *   status_ret - Output: status code
 *
 * Original address: 0x00E00A90
 */
void APP_$DEMUX(void *pkt_info, uint16_t *ec_ptr1, uint16_t *ec_ptr2,
                int8_t *flags, status_$t *status_ret);

/*
 * APP_$STD_OPEN - Open standard application channel
 *
 * Opens the standard application protocol channel via XNS IDP.
 * This is called during system initialization to set up the
 * primary application-level network service.
 *
 * Initializes the exclusion lock and registers APP_$DEMUX as
 * the packet handler for protocol 0x0499.
 *
 * Original address: 0x00E00B92
 */
void APP_$STD_OPEN(void);

/*
 * APP_$STD_IDP_CHANNEL - Standard application IDP channel number
 *
 * 0xFFFF when no channel is open.  Also consulted by ROUTE_$SERVICE when
 * registering a new port with the IDP channels.
 *
 * Original address: 0xE1DC20
 */
#ifndef APP_$STD_IDP_CHANNEL
#if defined(ARCH_M68K)
#define APP_$STD_IDP_CHANNEL (*(uint16_t *)0xE1DC20)
#else
extern uint16_t APP_$STD_IDP_CHANNEL;
#endif
#endif

#endif /* APP_H */
