/*
 * NETWORK - Internal Header
 *
 * Internal definitions and helper prototypes for the NETWORK subsystem.
 * This file should be included by all .c files within the network/ directory.
 */

#ifndef NETWORK_INTERNAL_H
#define NETWORK_INTERNAL_H

#include "network/network.h"

/*
 * NODE_$ME - This node's identifier
 *
 * The low 20 bits of the local node's network address. Used to detect
 * loopback/local destination requests.
 *
 * Original address: 0xE245A4
 */
#if defined(M68K)
#define NODE_$ME (*(uint32_t *)0xE245A4)
#else
extern uint32_t NODE_$ME;
#endif

/*
 * NETWORK_$LOOPBACK_FLAG - Loopback mode indicator
 *
 * If negative (bit 7 set), network operations should use the local node
 * as the destination instead of the specified address.
 *
 * Original address: 0xE24C44
 */
#if defined(M68K)
#define NETWORK_$LOOPBACK_FLAG (*(int8_t *)0xE24C44)
#else
extern int8_t NETWORK_$LOOPBACK_FLAG;
#endif

/*
 * Network command codes
 */
#define NETWORK_CMD_RING_INFO 0x0E /* Get ring information */

/*
 * Network status codes (module 0x11)
 */
#define status_$network_no_available_sockets 0x00110005
#define status_$network_unexpected_reply_type 0x0011000B
#define status_$network_unknown_network 0x00110017
#define status_$network_too_many_networks_in_internet 0x00110018
#define status_$network_too_many_transmit_retries 0x00110011

/*
 * Network table - maps network indices to network IDs
 *
 * The table has 64 entries (indices 1-63, with 0 being special/unused).
 * Each entry is 8 bytes:
 *   - 4 bytes: reference count (number of uses of this network)
 *   - 4 bytes: network ID (the actual network identifier)
 *
 * A network address contains the network index in bits 4-9 (mask 0x3F0).
 * Extract with: (addr & 0x3F0) >> 4
 *
 * Data layout in m68k memory:
 *   0xE24934: refcount[0], refcount[1], ... (each 8 bytes apart)
 *   0xE24938: net_id[0], net_id[1], ...     (each 8 bytes apart)
 */
typedef struct network_table_entry_t {
    uint32_t refcount;      /* Number of references to this network */
    uint32_t net_id;        /* Network identifier */
} network_table_entry_t;

#define NETWORK_TABLE_SIZE 64

/* Network table - 64 entries indexed by network index */
extern network_table_entry_t NETWORK_$NET_TABLE[NETWORK_TABLE_SIZE];

/* Extract network index from a network address value */
#define NETWORK_INDEX_MASK  0x3F0
#define NETWORK_INDEX_SHIFT 4
#define NETWORK_GET_INDEX(addr) (((addr) & NETWORK_INDEX_MASK) >> NETWORK_INDEX_SHIFT)

/*
 * Network globals
 */
extern int16_t NETWORK_$RETRY_TIMEOUT; /* 0xE24C18 - timeout for retries */

/*
 * NETWORK_$LOCK - Spin lock for network data protection
 *
 * Located at network data base + 0x2A4 = 0xE24BA0
 */
extern void *NETWORK_$LOCK;

/*
 * Socket pointer array (for event count access)
 */
extern void *SOCK_$SOCKET_PTR[]; /* 0xE28DB4 */

/*
 * network_$send_request - Send a network request packet
 *
 * Internal helper that builds and sends a network request packet.
 * Handles retries on transmission failure.
 *
 * @param net_handle       Network handle
 * @param sock_num         Socket number
 * @param pkt_id           Packet ID
 * @param cmd_buf          Command buffer
 * @param cmd_len          Command length
 * @param param_hi         High word of param4
 * @param param_lo         Combined param4_lo and param5
 * @param retry_count_out  Output: max retry count
 * @param timeout_out      Output: timeout value
 * @param status_ret       Output: status code
 *
 * Original address: 0x00E0F5F4
 */
void network_$send_request(void *net_handle, int16_t sock_num, int16_t pkt_id,
                           int16_t *cmd_buf, int16_t cmd_len, int16_t param_hi,
                           uint32_t param_lo, uint16_t *retry_count_out,
                           int16_t *timeout_out, status_$t *status_ret);

/*
 * network_$wait_response - Wait for network response
 *
 * Internal helper that waits for a response packet matching the given
 * packet ID. Uses event count waiting for efficient blocking.
 *
 * @param sock_num         Socket number
 * @param pkt_id           Packet ID to match
 * @param timeout          Timeout in clock ticks
 * @param event_count      Event count pointer (updated on each iteration)
 * @param resp_buf         Response buffer output
 * @param resp_len_out     Output: response length
 * @param data_bufs        Output: data buffer pointers
 * @param data_len_out     Output: data length
 *
 * @return Negative (0xFF) on success, 0 on timeout
 *
 * Original address: 0x00E0F746
 */
int8_t network_$wait_response(int16_t sock_num, int16_t pkt_id,
                              uint16_t timeout, int32_t *event_count,
                              int16_t *resp_buf, int16_t *resp_len_out,
                              uint32_t *data_bufs, uint16_t *data_len_out);

/*
 * network_$do_request - Send a network command and receive response
 *
 * Internal helper function that sends a command to a network partner
 * and waits for a response. Handles packet allocation, transmission,
 * and response collection.
 *
 * @param net_handle     Network handle/connection
 * @param cmd_buf        Command buffer to send
 * @param cmd_len        Command length (in bytes)
 * @param param4         Reserved (pass 0)
 * @param param5         Reserved (pass 0)
 * @param param6         Reserved (pass 0)
 * @param resp_buf       Response buffer
 * @param resp_info      Response info output
 * @param status_ret     Output: status code
 *
 * Original address: 0x00E0F86C
 */
void network_$do_request(void *net_handle, void *cmd_buf, int16_t cmd_len,
                         uint32_t param4, uint16_t param5, int16_t param6,
                         void *resp_buf, void *resp_info,
                         status_$t *status_ret);

#endif /* NETWORK_INTERNAL_H */
