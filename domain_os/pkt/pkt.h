/*
 * PKT - Packet Building Module
 *
 * This module provides functions for building and managing network packet
 * headers for Domain/OS internet protocol communication.
 *
 * The PKT module handles:
 * - Building and parsing internet packet headers
 * - Sending and receiving internet packets
 * - Packet ID generation
 * - Node visibility tracking (for detecting unresponsive nodes)
 * - Ping service for network diagnostics
 */

#ifndef PKT_H
#define PKT_H

#include "base/base.h"

/*
 * ============================================================================
 * Status Codes (module 0x11 = NETWORK)
 * ============================================================================
 */
#define status_$network_data_length_too_large           0x0011001C
#define status_$network_request_denied_by_local_node    0x0011000E
#define status_$network_msg_exceeds_max_size            0x0011001E
#define status_$network_message_header_too_big          0x0011000A
#define status_$network_no_more_free_sockets            0x0011000C
#define status_$network_remote_node_failed_to_respond   0x00110007
#define status_$network_buffer_queue_is_empty              0x00110006

/*
 * ============================================================================
 * Initialization
 * ============================================================================
 */

/*
 * PKT_$INIT - Initialize the packet module
 *
 * Initializes the PKT subsystem and creates the ping server process.
 * Must be called during system initialization.
 *
 * Original address: 0x00E2F84C
 */
void PKT_$INIT(void);

/*
 * ============================================================================
 * Packet ID Generation
 * ============================================================================
 */

/*
 * PKT_$NEXT_ID - Get next short packet ID
 *
 * Returns a unique short (16-bit) packet ID. IDs cycle from 1 to 64000.
 * Thread-safe via spin lock.
 *
 * Returns:
 *   Next available packet ID (1-64000)
 *
 * Original address: 0x00E1248E
 */
int16_t PKT_$NEXT_ID(void);

/*
 * PKT_$NEXT_LONG_ID - Get next long packet ID
 *
 * Returns a unique long (32-bit) packet ID. IDs increment without wrap.
 * Thread-safe via spin lock.
 *
 * Returns:
 *   Next available long packet ID
 *
 * Original address: 0x00E124DC
 */
int32_t PKT_$NEXT_LONG_ID(void);

/*
 * ============================================================================
 * Packet Header Building and Parsing
 * ============================================================================
 */

/*
 * pkt_$info_t - the 0x20-byte "packet info" record every PKT_$SEND_INTERNET /
 * PKT_$BLD_INTERNET_HDR caller hands over by address.
 *
 * Recovered from PKT_$BLD_INTERNET_HDR (0x00E1202C), which is the only reader
 * of the whole record, and from the two loaded instances inside PKT_$DATA
 * (0x00E24D04 and 0x00E24D24), which are 0x20 bytes apart and identical apart
 * from their first word:
 *
 *   +0x00  flags          0x00E1206A  move.b (0x1,A3),(0x4,A2)   low byte
 *                         0x00E12298  move.b (0x1,A3),(0xe,A2)
 *                         written as a word by PKT_$PING_SERVER
 *                         (0x00E12CE6 "move.w D4w,(0x88,A5)")
 *   +0x02  routing_type   0x00E1208E  cmpi.w #0x2,(0x2,A3)   internet
 *                         0x00E12262  cmpi.w #0x1,(0x2,A3)   local/loopback
 *                         0x00E1228A  move.b (0x3,A3),(0xc,A2)  low byte
 *   +0x04  addr_type      0x00E1221E  cmpi.w #0x2,(0x4,A3)
 *   +0x06  protocol       0x00E12230  move.w (0x6,A3),(0x4a,A2)
 *                         0x00E1223A  move.b (0x7,A3),(0x2d,A2)  low byte
 *   +0x08  retry_limit    0x00E126A0  tst.w (0x8,A4)  in PKT_$SEND_INTERNET
 *   +0x0A  field_0a       0x00E121AE  move.b (0xb,A3),(0x2c,A2)  low byte
 *   +0x0C  field_0c       0x00E121A0  move.w (0xc,A3),(0x28,A2)
 *   +0x0E  addr[16]       0x00E12248-0x00E12256, copied into the header at
 *                         +0x4C when protocol == 0x29
 */
typedef struct pkt_$info_t {
    uint16_t    flags;          /* 0x00 */
    int16_t     routing_type;   /* 0x02: 1 = local, 2 = internet */
    int16_t     addr_type;      /* 0x04: 2 = long (16-byte) address present */
    uint16_t    protocol;       /* 0x06 */
    uint16_t    retry_limit;    /* 0x08: 0 or 0xFFFF = take the builder's hint */
    uint16_t    field_0a;       /* 0x0A */
    uint16_t    field_0c;       /* 0x0C */
    uint8_t     addr[16];       /* 0x0E */
    uint16_t    pad_1e;         /* 0x1E */
} pkt_$info_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(pkt_$info_t, routing_type) == 0x02, "pkt_$info_t.routing_type");
_Static_assert(offsetof(pkt_$info_t, addr_type)    == 0x04, "pkt_$info_t.addr_type");
_Static_assert(offsetof(pkt_$info_t, protocol)     == 0x06, "pkt_$info_t.protocol");
_Static_assert(offsetof(pkt_$info_t, retry_limit)  == 0x08, "pkt_$info_t.retry_limit");
_Static_assert(offsetof(pkt_$info_t, field_0c)     == 0x0C, "pkt_$info_t.field_0c");
_Static_assert(offsetof(pkt_$info_t, addr)         == 0x0E, "pkt_$info_t.addr");
_Static_assert(sizeof(pkt_$info_t) == 0x20, "pkt_$info_t must be 32 bytes");
#endif

/*
 * PKT_$BLD_INTERNET_HDR - Build an internet packet header
 *
 * Builds a complete internet packet header for network transmission.
 * Handles both local (loopback) and remote destinations.
 * Validates packet sizes and performs route lookup.
 *
 * @param routing_key   Routing key for next-hop lookup
 * @param dest_node     Destination node ID
 * @param dest_sock     Destination socket number
 * @param src_node_or   Explicit source node or -1 for default
 * @param src_node      Source node ID
 * @param src_sock      Source socket number
 * @param pkt_info      Packet info structure pointer
 * @param request_id    Request ID
 * @param template      Packet template data
 * @param template_len  Template length (0x00E12040 "move.w (0x26,A6),D2w")
 * @param data_len      Data length   (0x00E12044 "move.w (0x28,A6),D3w")
 * @param port_out      In/out: port number.  RIP_$FIND_NEXTHOP writes it on
 *                      the internet path (0x00E120C0), the local path clears
 *                      it (0x00E12288), and 0x00E12102 "move.w (A1),D7w"
 *                      reads it back to index ROUTE_$PORTP.
 * @param hdr_buf       The header buffer to fill in (the VA NETWORK_$GETHDR
 *                      returned); passed by value, 0x00E12048
 *                      "movea.l (0x2e,A6),A2".
 * @param len_out       Output: total packet length (0x00E122D4)
 * @param retry_hint    Output: send retry limit.  Unconditionally set to 5
 *                      (0x00E1230E "move.w #0x5,(A0)"), so it may not be NULL.
 * @param timeout_out   Output: response timeout in clock ticks.
 *                      Unconditionally set to 4 (0x00E1231A
 *                      "move.w #0x4,(A1)"), so it may not be NULL.
 * @param status_ret    Output: status code
 *
 * Seventeen arguments plus a 2-byte Pascal function-result slot; the caller
 * pops 0x3C bytes (0x00E12712 "lea (0x3c,SP),SP").
 *
 * Original address: 0x00E1202C
 */
void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           void *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, uint32_t *hdr_buf, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret);

/*
 * PKT_$BRK_INTERNET_HDR - Break down (parse) an internet packet header
 *
 * Parses a received internet packet header and extracts addressing
 * and protocol information.
 *
 * @param hdr_ptr       Pointer to received packet header (0x00E12330)
 * @param hdr_len        NOT READ.  Nothing between 0x00E12328 and 0x00E1248C
 *                      touches (0xC,A6); RIP_$SERVER passes
 *                      sock_$pkt_info_t.hdr_len there (0x00E68ABE) all the
 *                      same, so the word has to stay in the signature.
 * @param routing_key   Output: routing key   (D2, 0x00E12334)
 * @param dest_node     Output: destination node ID (D3, 0x00E12338)
 * @param dest_sock     Output: destination socket, a WORD (D4, 0x00E1233C;
 *                      written by "move.w (0x38,A2),(A3)" at 0x00E123DE)
 * @param src_node_or   Output: source node override (D5, 0x00E12340)
 * @param src_node      Output: source node ID (0x1E,A6, 0x00E123AC/0x00E123EE)
 * @param src_sock      Output: source socket, a WORD (A1, 0x00E12344)
 * @param info_out      Output: the 30-byte packet-info record (A0,
 *                      0x00E12348): +0x00 = header byte 0x0E, +0x02 = header
 *                      byte 0x0C, +0x04/+0x06 the protocol pair, +0x0E the
 *                      16-byte tail copied when the subtype is 0x29
 * @param id_out        Output: request ID, header word 0x16 (A3, 0x00E12362)
 * @param data_buf      Output: the payload, copied by PKT_$DAT_COPY at
 *                      0x00E1247C
 * @param data_max      Maximum payload bytes (a WORD, 0x00E1234E)
 * @param data_len      In/out: header word 0x12 on the way in
 *                      (0x00E1236E), the number of bytes actually copied on
 *                      the way out (0x00E12482)
 * @param status_ret    Output: status code; 0x00110024 when the header plus
 *                      payload will not fit a 0x3B8-byte buffer
 *                      (0x00E1245E)
 *
 * Fourteen arguments; the one caller pops 0x34 bytes (0x00E68ACA
 * "lea (0x34,SP),SP") and the argument offsets run 0x08, 0x0C, 0x0E, 0x12,
 * 0x16, 0x1A, 0x1E, 0x22, 0x26, 0x2A, 0x2E, 0x32, 0x34, 0x38.
 *
 * Original address: 0x00E12328
 */
void PKT_$BRK_INTERNET_HDR(void *hdr_ptr, uint16_t hdr_len,
                           uint32_t *routing_key, uint32_t *dest_node,
                           uint16_t *dest_sock, uint32_t *src_node_or,
                           uint32_t *src_node, uint16_t *src_sock,
                           uint16_t *info_out, uint16_t *id_out,
                           void *data_buf, uint16_t data_max,
                           uint16_t *data_len, status_$t *status_ret);

/*
 * ============================================================================
 * Packet Data Buffer Management
 * ============================================================================
 */

/*
 * PKT_$COPY_TO_PA - Copy data to physical address buffers
 *
 * Copies data from a virtual address to network buffer pages.
 * Allocates necessary buffer pages and maps them.
 *
 * @param src_va        Source virtual address
 * @param len           Length of data to copy
 * @param buffers_out   Output: array of buffer physical addresses
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1251C
 */
void PKT_$COPY_TO_PA(char *src_va, uint16_t len, uint32_t *buffers_out,
                     status_$t *status_ret);

/*
 * PKT_$DUMP_DATA - Release packet data buffers
 *
 * Returns data buffers allocated by PKT_$COPY_TO_PA back to the pool.
 *
 * @param buffers       Array of buffer physical addresses
 * @param len           Total length of data (determines how many buffers)
 *
 * Original address: 0x00E127E6
 */
void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len);

/*
 * PKT_$DAT_COPY - Copy data from network buffers
 *
 * Copies data from network buffer pages to a destination virtual address.
 *
 * @param buffers       Array of buffer physical addresses
 * @param len           Length of data to copy
 * @param dest_va       Destination virtual address
 *
 * Original address: 0x00E12834
 */
void PKT_$DAT_COPY(uint32_t *buffers, int16_t len, char *dest_va);

/*
 * ============================================================================
 * Packet Sending and Receiving
 * ============================================================================
 */

/*
 * PKT_$SEND_INTERNET - Send an internet packet
 *
 * Sends a packet over the network. Handles retries on failure.
 *
 * @param routing_key   Routing key
 * @param dest_node     Destination node ID
 * @param dest_sock     Destination socket
 * @param src_node_or   Source node override (-1 for default)
 * @param src_node      Source node ID
 * @param src_sock      Source socket
 * @param pkt_info      Packet info structure
 * @param request_id    Request ID
 * @param template      Request template
 * @param template_len  Template length
 * @param data          Data buffer virtual address (only read when
 *                      data_len > 0: 0x00E12686 "tst.w D6w / ble")
 * @param data_len      Data length
 * @param retry_hint    Output: retry limit hint.  PKT_$BLD_INTERNET_HDR
 *                      stores 5 here (0x00E1230E "move.w #0x5,(A0)") and
 *                      PKT_$SEND_INTERNET adopts it as the retry limit when
 *                      pkt_info's own count is <= 0 (0x00E12724
 *                      "move.w (A3),D4w").  Never NULL.
 * @param timeout_out   Output: response timeout in clock ticks.
 *                      PKT_$BLD_INTERNET_HDR stores 4 here (0x00E12316 /
 *                      0x00E1231A "move.w #0x4,(A1)").  Never NULL.
 * @param status_ret    Output: status code
 *
 * Fifteen arguments plus a 2-byte Pascal function-result slot; the caller
 * pops 0x34 bytes (0x00E12AC2 "lea (0x34,SP),SP") and the argument offsets in
 * the prologue run 0x08, 0x0C, 0x10, 0x12, 0x16, 0x1A, 0x1C, 0x20, 0x22,
 * 0x26, 0x28, 0x2C, 0x2E, 0x32, 0x36.
 *
 * Original address: 0x00E1264E
 */
void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                        int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret);

/*
 * PKT_$SAR_INTERNET - Send and receive internet packet
 *
 * Sends a packet and waits for a response. Handles retries and
 * node visibility tracking.
 *
 * @param routing_key   Routing key
 * @param dest_node     Destination node ID
 * @param dest_sock     Destination socket
 * @param pkt_info      Packet info structure (also receives retry count used)
 * @param timeout       Timeout in clock ticks
 * @param req_template  Request template
 * @param req_tpl_len   Request template length
 * @param req_data      Request data buffer
 * @param req_data_len  Request data length
 * @param resp_buf      Response buffer
 * @param resp_tpl_buf  Response template buffer
 * @param resp_tpl_max  Maximum response template length
 * @param resp_tpl_len  Output: actual response template length
 * @param resp_data_buf Response data buffer
 * @param resp_data_max Maximum response data length
 * @param resp_data_len Output: actual response data length
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E71EC4
 */
void PKT_$SAR_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                       void *pkt_info, int16_t timeout,
                       void *req_template, uint16_t req_tpl_len,
                       void *req_data, uint16_t req_data_len,
                       void *resp_buf, char *resp_tpl_buf, uint16_t resp_tpl_max,
                       uint16_t *resp_tpl_len, void *resp_data_buf, uint16_t resp_data_max,
                       uint16_t *resp_data_len, status_$t *status_ret);

/*
 * ============================================================================
 * Node Visibility Tracking
 * ============================================================================
 */

/*
 * PKT_$RECENTLY_MISSING - Check if a node is in the recently missing list
 *
 * Checks if a node has recently failed to respond to network requests.
 * Used to avoid unnecessary retries to unresponsive nodes.
 *
 * @param node_id       Node ID to check
 *
 * Returns:
 *   true (0xFF) if node is in the recently missing list, false otherwise
 *   (0x00E128D0 "clr.b D0b" / 0x00E128E2 "st D0b")
 *
 * Original address: 0x00E128BA
 */
boolean PKT_$RECENTLY_MISSING(uint32_t node_id);

/*
 * PKT_$NOTE_VISIBLE - Update node visibility status
 *
 * Updates the visibility tracking for a node based on whether it
 * responded to a request.
 *
 * @param node_id       Node ID to update
 * @param is_visible    Non-zero if node responded, 0 if failed to respond
 *
 * Original address: 0x00E128F6
 */
void PKT_$NOTE_VISIBLE(uint32_t node_id, boolean is_visible);

/*
 * pkt_$net_addr_t - the 8-byte network/node pair PKT_$LIKELY_TO_ANSWER is
 * handed.  Only these two longwords are read (0x00E129B4 "move.l (A0),..."
 * and 0x00E129C0 "move.l (0x4,A0),D0").
 */
typedef struct pkt_$net_addr_t {
    uint32_t    network;    /* 0x00: routing key / network number */
    uint32_t    node;       /* 0x04: node id; only the low 20 bits matter */
} pkt_$net_addr_t;

/*
 * PKT_$LIKELY_TO_ANSWER - Check if node is likely to respond
 *
 * Determines if a node is likely to respond to requests. May send
 * a ping packet to verify the node is reachable.
 *
 * @param addr_info     pkt_$net_addr_t for the node in question.  Declared
 *                      void * because callers hand it several different
 *                      record types.
 * @param status_ret    Output: status code
 *
 * Returns:
 *   true (0xFF) if node is likely to respond, false otherwise.  The result is
 *   a byte: every caller does "tst.b D0b" then bmi/bpl (0x00E0F9AC,
 *   0x00E614FA, 0x00E7205A).
 *
 * Original address: 0x00E1299E
 */
boolean PKT_$LIKELY_TO_ANSWER(void *addr_info, status_$t *status_ret);

#endif /* PKT_H */
