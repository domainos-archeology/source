/*
 * SOCK - Socket Management Module
 *
 * This module provides socket management for network communication.
 */

#ifndef SOCK_H
#define SOCK_H

#include "base/base.h"

/*
 * Socket flags
 */
#define SOCK_FLAG_USER          0x20000     /* User-mode socket */
#define SOCK_FLAG_KERNEL        0x00001     /* Kernel-mode socket */

/*
 * SOCK_$OPEN - Open a socket for a well-known service
 *
 * Opens a socket on a specific socket number for a well-known service.
 * The socket is bound to the specified socket number.
 *
 * @param sock_num      Socket number to open
 * @param flags         Socket flags
 * @param buffer_size   Receive buffer size
 *
 * Returns:
 *   Negative on success
 *   Non-negative on failure
 *
 * Original address: 0x00E15D8C
 */
int8_t SOCK_$OPEN(uint16_t sock_num, uint32_t flags, uint16_t buffer_size);

/*
 * SOCK_$ALLOCATE - Allocate a socket with automatic socket number
 *
 * Allocates a socket with an automatically assigned socket number.
 *
 * @param sock_ret      Output: allocated socket number
 * @param flags         Socket flags
 * @param buffer_size   Receive buffer size
 *
 * Returns:
 *   Negative on success
 *   Non-negative on failure
 *
 * Original address: 0x00E15E62
 */
int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t flags, uint32_t buffer_size);

/*
 * SOCK_$ALLOCATE_USER - Allocate a user-mode socket
 *
 * @param sock_ret      Output: allocated socket number
 * @param flags         Socket flags
 * @param buffer_size   Receive buffer size
 *
 * Returns:
 *   Negative on success
 *   Non-negative on failure
 *
 * Original address: 0x00E15F14
 */
int8_t SOCK_$ALLOCATE_USER(uint16_t *sock_ret, uint32_t flags, uint32_t buffer_size);

/*
 * SOCK_$CLOSE - Close a socket
 *
 * Closes an open socket and frees associated resources.
 *
 * @param sock_num      Socket number to close
 *
 * Original address: 0x00E15F72
 */
void SOCK_$CLOSE(uint16_t sock_num);

/*
 * SOCK_$GET - Get next packet from socket receive queue
 *
 * Retrieves the next packet from a socket's receive queue.
 *
 * @param sock_num      Socket number
 * @param pkt_ret       Output: pointer to packet data
 *
 * Returns:
 *   Negative (< 0): Success, packet available in *pkt_ret
 *   Non-negative (>= 0): No packet available
 *
 * Original address: 0x00E16070
 */
int8_t SOCK_$GET(uint16_t sock_num, void **pkt_ret);

/*
 * SOCK_$PUT - Put packet on socket receive queue
 *
 * Queues a packet for delivery to a socket.
 *
 * @param sock_num      Socket number
 * @param pkt_ptr       Pointer to packet pointer
 * @param flags         Flags
 * @param ec_param1     Event count parameter 1
 * @param ec_param2     Event count parameter 2
 *
 * Returns:
 *   Negative (< 0): Success, packet queued
 *   Non-negative (>= 0): Queue full or error
 *
 * Original address: 0x00E1614E
 */
int8_t SOCK_$PUT(uint16_t sock_num, void **pkt_ptr, uint16_t flags,
                 uint16_t ec_param1, uint16_t ec_param2);

/*
 * SOCK_$INIT - Initialize socket subsystem
 *
 * Original address: 0x00E2FDF0
 */
void SOCK_$INIT(void);

#endif /* SOCK_H */
