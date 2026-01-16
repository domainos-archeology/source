/*
 * PKT - Packet Building Module
 *
 * This module provides functions for building network packet headers.
 */

#ifndef PKT_H
#define PKT_H

#include "base/base.h"

/*
 * Status codes for PKT module (module 0x11 = NETWORK)
 */
#define status_$network_data_length_too_large       0x0011001C
#define status_$network_request_denied_by_local_node 0x0011000E
#define status_$network_msg_exceeds_max_size        0x0011001E
#define status_$network_message_header_too_big      0x0011000A

/*
 * PKT_$BLD_INTERNET_HDR - Build an internet packet header
 *
 * Builds a complete internet packet header for network transmission.
 * Handles both local (loopback) and remote destinations.
 *
 * @param param1      First parameter (routing info)
 * @param dest_node   Destination node ID
 * @param dest_sock   Destination socket number
 * @param src_node_or Explicit source node or -1 for default
 * @param src_node    Source node ID
 * @param src_sock    Source socket number
 * @param pkt_info    Packet info structure pointer
 * @param param8      Additional parameter
 * @param template    Packet template data
 * @param hdr_len     Header length
 * @param protocol    Protocol number (e.g., 0x3F6 for logging)
 * @param port_out    Output: port number
 * @param hdr_buf     Output: header buffer pointer
 * @param len_out     Output: packet length
 * @param param15     Output parameter
 * @param param16     Output parameter
 * @param status_ret  Output: status code
 *
 * Original address: 0x00E1202C
 */
void PKT_$BLD_INTERNET_HDR(uint32_t param1, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           void *pkt_info, uint16_t param8,
                           void *template, uint16_t hdr_len, uint16_t protocol,
                           int16_t *port_out, uint32_t *hdr_buf, uint16_t *len_out,
                           uint16_t *param15, uint16_t *param16, status_$t *status_ret);

#endif /* PKT_H */
