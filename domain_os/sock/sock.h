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
 * SOCK_$GET - Get socket state
 *
 * @param sock_num      Socket number
 * @param state_ret     Output: socket state
 *
 * Original address: 0x00E16070
 */
void SOCK_$GET(uint16_t sock_num, void *state_ret);

/*
 * SOCK_$PUT - Put data on socket
 *
 * @param sock_num      Socket number
 * @param data          Data to send
 * @param len           Data length
 *
 * Original address: 0x00E1614E
 */
void SOCK_$PUT(uint16_t sock_num, void *data, uint16_t len);

/*
 * SOCK_$INIT - Initialize socket subsystem
 *
 * Original address: 0x00E2FDF0
 */
void SOCK_$INIT(void);

#endif /* SOCK_H */
