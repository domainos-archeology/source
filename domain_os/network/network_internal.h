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
#define NODE_$ME        (*(uint32_t *)0xE245A4)
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
#define NETWORK_$LOOPBACK_FLAG  (*(int8_t *)0xE24C44)
#else
extern int8_t NETWORK_$LOOPBACK_FLAG;
#endif

/*
 * Network command codes
 */
#define NETWORK_CMD_RING_INFO   0x0E    /* Get ring information */

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
                         void *resp_buf, void *resp_info, status_$t *status_ret);

#endif /* NETWORK_INTERNAL_H */
