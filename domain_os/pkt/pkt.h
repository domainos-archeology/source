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
 * @param hdr_len       Header length
 * @param protocol      Protocol number (e.g., 0x3F6 for logging)
 * @param port_out      Output: port number
 * @param hdr_buf       Output: header buffer pointer
 * @param len_out       Output: packet length
 * @param param15       Output parameter
 * @param param16       Output parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1202C
 */
void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           void *pkt_info, uint16_t request_id,
                           void *template, uint16_t hdr_len, uint16_t protocol,
                           int16_t *port_out, uint32_t *hdr_buf, uint16_t *len_out,
                           uint16_t *param15, uint16_t *param16, status_$t *status_ret);

/*
 * PKT_$BRK_INTERNET_HDR - Break down (parse) an internet packet header
 *
 * Parses a received internet packet header and extracts addressing
 * and protocol information.
 *
 * @param hdr_ptr       Pointer to received packet header
 * @param routing_key   Output: routing key
 * @param dest_node     Output: destination node ID
 * @param dest_sock     Output: destination socket
 * @param src_node_or   Output: source node override
 * @param src_node      Output: source node ID
 * @param src_sock      Output: source socket
 * @param info_out      Output: packet info structure
 * @param id_out        Output: request ID
 * @param template_len  Maximum template buffer length
 * @param template_out  Output: template length written
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E12328
 */
void PKT_$BRK_INTERNET_HDR(void *hdr_ptr, uint32_t *routing_key, uint32_t *dest_node,
                           uint16_t *dest_sock, uint32_t *src_node_or, uint32_t *src_node,
                           uint16_t *src_sock, uint16_t *info_out, uint16_t *id_out,
                           char *template_out, uint16_t template_len,
                           uint16_t *template_out_len, status_$t *status_ret);

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
