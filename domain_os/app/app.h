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

#endif /* APP_H */
