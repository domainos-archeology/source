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
#include "xns/xns.h"   /* xns_$pkt_desc_t */

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */


/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * app_$reply_hdr_t - the eight-byte prefix of the application reply record
 * app_$receive_rec_t.reply points at.
 *
 * APP_$RECEIVE builds it on the Domain internet path at
 * 0x00E00980-0x00E009AC, through A0 = rec.reply:
 *   0x00E00984  move.w #0x118,(A0)              magic
 *   0x00E009AC  move.w (0x12,A1),(0x2,A0)       template_len, off the
 *                                               received header + 0x12
 *   0x00E009A6  move.w (-0x16,A6),(0x4,A0)      data_len, the payload byte
 *                                               count APP_$RECEIVE computed
 *   0x00E009A0  move.w (0x16,A1),(0x6,A0)       request_id, off the received
 *                                               header + 0x16
 * On the XNS path the reply is the received header + 0x1E (0x00E008A8) and
 * the same four words are already in the packet.
 *
 * Every protocol that calls APP_$RECEIVE reads these four words at these
 * offsets and then continues into its own tail:
 *   msg_$reply_hdr_t       (msg/msg_internal.h)      tail to 0x17
 *   asknode_$reply_hdr_t   (asknode/asknode_internal.h) tail to 0x15
 *   rem_file/ and rip/     use this record by itself
 * MSG spells the +0x06 word "message type" and ASKNODE spells it "reply id";
 * they are the same cell, matched against the request id the sender chose.
 * (source-ca0z)
 */
typedef struct app_$reply_hdr_t {
    uint16_t    magic;          /* 0x00: 0x0118 (0x00E00984); only asknode
                                 *       ever looks at it */
    uint16_t    template_len;   /* 0x02: reply/template byte count */
    uint16_t    data_len;       /* 0x04: bulk payload byte count */
    int16_t     request_id;     /* 0x06: the id the reply is matched on */
} __attribute__((packed)) app_$reply_hdr_t;

_Static_assert(offsetof(app_$reply_hdr_t, magic)        == 0x00, "app_reply.magic");
_Static_assert(offsetof(app_$reply_hdr_t, template_len) == 0x02, "app_reply.template_len");
_Static_assert(offsetof(app_$reply_hdr_t, data_len)     == 0x04, "app_reply.data_len");
_Static_assert(offsetof(app_$reply_hdr_t, request_id)   == 0x06, "app_reply.request_id");
_Static_assert(sizeof(app_$reply_hdr_t) == 8, "app_$reply_hdr_t must be 8 bytes");

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
    uint32_t    reply;              /* 0x00: a target VIRTUAL ADDRESS, not a C
                                     *       pointer - 0x00E008A8
                                     *       "moveq #0x1e,D0 / add.l
                                     *       (-0x40,A6),D0 / move.l D0,(A2)"
                                     *       stores an address computed from
                                     *       sock_$pkt_info_t.hdr, which is
                                     *       itself a VA.  A real pointer is
                                     *       eight bytes on a 64-bit host and
                                     *       would push every later field out
                                     *       of place.  Use ARCH_VA_TO_PTR. */
    uint32_t    data;               /* 0x04: likewise (0x00E008B0 / 0x00E0093C) */
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

/* No pointer fields, so the layout holds on the host too. */
_Static_assert(offsetof(app_$receive_rec_t, data)       == 0x04, "app_rcv.data");
_Static_assert(offsetof(app_$receive_rec_t, data_pages) == 0x08, "app_rcv.data_pages");
_Static_assert(offsetof(app_$receive_rec_t, hdr_f06)    == 0x18, "app_rcv.hdr_f06");
_Static_assert(offsetof(app_$receive_rec_t, hdr_f12)    == 0x1C, "app_rcv.hdr_f12");
_Static_assert(offsetof(app_$receive_rec_t, src_addr)   == 0x20, "app_rcv.src_addr");
_Static_assert(offsetof(app_$receive_rec_t, src_port)   == 0x24, "app_rcv.src_port");
_Static_assert(offsetof(app_$receive_rec_t, flags_hi)   == 0x26, "app_rcv.flags_hi");
_Static_assert(offsetof(app_$receive_rec_t, flags_lo)   == 0x28, "app_rcv.flags_lo");
_Static_assert(sizeof(app_$receive_rec_t) == 0x2C, "app_$receive_rec_t must be 44 bytes");

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
 * The channel demux vector APP_$STD_OPEN installs (its address is taken at
 * 0x00E00BBA).  XNS_IDP_$OS_DEMUX calls it through xns_$channel_t.demux at
 * 0x00E18658 with five longword arguments, so the signature is the
 * xns_$demux_fn_t one: the packet descriptor it built, the receiving ROUTE
 * port's type and socket words, the MAC broadcast boolean, and a status cell.
 *
 * If the packet cannot be delivered to the socket named in the application
 * header, and that socket was APP_SOCK_TYPE_FILE, it is retried on
 * APP_SOCK_TYPE_OVERFLOW; if nothing takes it, the header and data buffers
 * are returned to the pool.
 *
 * Parameters:
 *   pkt            - the descriptor XNS_IDP_$OS_DEMUX built
 *   port_type      - the ROUTE port's type word   (SOCK_$PUT argument 4)
 *   port_socket    - the ROUTE port's socket word (SOCK_$PUT argument 5)
 *   mac_broadcast  - Domain boolean, the frame arrived as a MAC broadcast
 *   status_ret     - Output: status code, only ever cleared (0x00E00AAC)
 *
 * Original address: 0x00E00A90
 */
void APP_$DEMUX(xns_$pkt_desc_t *pkt, uint16_t *port_type,
                uint16_t *port_socket, boolean *mac_broadcast,
                status_$t *status_ret);

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
