/*
 * APP - Application Protocol Module
 *
 * This module provides application-level network protocol handling.
 */

#ifndef APP_H
#define APP_H

#include "base/base.h"

/*
 * APP_$RECEIVE - Receive a packet on a socket
 *
 * Receives the next available packet from a socket.
 *
 * @param sock_num      Socket number
 * @param pkt_ret       Output: pointer to received packet header
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E00800
 */
void APP_$RECEIVE(uint16_t sock_num, void **pkt_ret, status_$t *status_ret);

/*
 * APP_$DEMUX - Demultiplex received packet
 *
 * Routes a received packet to the appropriate handler.
 *
 * Original address: 0x00E00A90
 */
void APP_$DEMUX(void);

/*
 * APP_$STD_OPEN - Standard socket open
 *
 * Original address: 0x00E00B92
 */
void APP_$STD_OPEN(void);

#endif /* APP_H */
