/*
 * SOCK - Socket Management Module (Public Header)
 *
 * This module provides socket management for network communication in Domain/OS.
 * Sockets are message queues that allow network packets to be delivered to
 * processes. Each socket has an associated event count for synchronization.
 *
 * Socket Number Allocation:
 *   - Sockets 0-31: Reserved for well-known services (statically bound)
 *   - Sockets 32-223: Dynamically allocated from free list
 *
 * Return Value Convention:
 *   - Negative (< 0, typically 0xFF): Success
 *   - Zero or positive (>= 0): Failure or empty queue
 */

#ifndef SOCK_H
#define SOCK_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * Socket flags for SOCK_$OPEN and SOCK_$ALLOCATE
 */
#define SOCK_FLAG_USER          0x40    /* User-mode socket (bit 6) */
#define SOCK_FLAG_KERNEL        0x00    /* Kernel-mode socket */

/*
 * Maximum valid socket number
 */
#define SOCK_MAX_NUMBER         0xDF    /* 223 */

/*
 * SOCK_$INIT - Initialize socket subsystem
 *
 * Initializes all socket descriptors, event counts, and the free list.
 * Must be called during kernel initialization before any other SOCK functions.
 *
 * Called by: NET_$INIT (or equivalent network initialization)
 *
 * Original address: 0x00E2FDF0
 */
void SOCK_$INIT(void);

/*
 * SOCK_$OPEN - Open a socket with a specific socket number
 *
 * Opens a socket for a well-known service (socket numbers 0-31) or
 * claims a specific socket number in the dynamic range (32-223).
 * The socket must not already be in use.
 *
 * @param sock_num        Socket number to open (1-223)
 * @param proto_bufpages  Packed value: (protocol << 16) | buffer_pages
 *                        - High word (bits 16-31): protocol type identifier
 *                        - Low word (bits 0-15): buffer page allocation
 * @param max_queue       Maximum receive queue depth
 *
 * @return Negative (0xFF) on success, 0 on failure (socket already in use)
 *
 * Note: If buffer_pages is non-zero, calls NETBUF_$ADD_PAGES to allocate buffers.
 *
 * Original address: 0x00E15D8C
 */
int8_t SOCK_$OPEN(uint16_t sock_num, uint32_t proto_bufpages, uint32_t max_queue);

/*
 * SOCK_$ALLOCATE - Allocate a socket from the free pool
 *
 * Allocates a socket with an automatically assigned socket number from
 * the dynamic range (32-223). The socket is taken from the free list.
 *
 * @param sock_ret        Output: allocated socket number (0 on failure)
 * @param proto_bufpages  Packed value: (protocol << 16) | buffer_pages
 *                        - High word (bits 16-31): protocol type identifier
 *                        - Low word (bits 0-15): buffer page allocation
 * @param max_queue       Maximum receive queue depth
 *
 * @return Negative (0xFF) on success, 0 on failure (no free sockets)
 *
 * Original address: 0x00E15E62
 */
int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages, uint32_t max_queue);

/*
 * SOCK_$ALLOCATE_USER - Allocate a user-mode socket
 *
 * Allocates a socket for user-mode use. User-mode sockets are tracked
 * separately and have a limit on the total number that can be allocated.
 * The socket is marked with the SOCK_FLAG_USER bit.
 *
 * Note: Parameters are passed as 16-bit pairs due to Pascal's 16-bit integer
 * limitations on some platforms. The function combines them internally:
 *   proto_bufpages = (proto_hi << 16) | proto_lo
 *   max_queue = (queue_hi << 16) | queue_lo
 *
 * @param sock_ret   Output: allocated socket number (0 on failure)
 * @param proto_hi   High word of proto_bufpages (protocol type)
 * @param proto_lo   Low word of proto_bufpages (buffer pages)
 * @param queue_hi   High word of max_queue
 * @param queue_lo   Low word of max_queue
 *
 * @return Negative (0xFF) on success, 0 on failure (no free user sockets)
 *
 * Original address: 0x00E15F14
 */
int8_t SOCK_$ALLOCATE_USER(uint16_t *sock_ret,
                           uint16_t proto_hi, uint16_t proto_lo,
                           uint16_t queue_hi, uint16_t queue_lo);

/*
 * SOCK_$CLOSE - Close a socket
 *
 * Closes an open socket, draining any queued packets and returning
 * allocated buffers to the pool. For dynamic sockets (>= 32), returns
 * the socket to the free list for reuse.
 *
 * @param sock_num      Socket number to close
 *
 * Original address: 0x00E15F72
 */
void SOCK_$CLOSE(uint16_t sock_num);

/*
 * SOCK_$GET - Get next packet from socket receive queue
 *
 * Retrieves the next packet from a socket's receive queue. The packet
 * information is copied to the provided buffer.
 *
 * @param sock_num      Socket number
 * @param pkt_info      Output: packet information buffer (40+ bytes)
 *
 * @return Negative (0xFF) if packet retrieved, 0 if queue empty
 *
 * Original address: 0x00E16070
 */
int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info);

/*
 * SOCK_$PUT - Put packet on socket receive queue
 *
 * Queues a packet for delivery to a socket. If successful, advances
 * the socket's event count to wake any waiting processes.
 *
 * @param sock_num      Socket number
 * @param pkt_ptr       Pointer to packet buffer pointer
 * @param flags         Flags (bit 7 = copy queue count to packet header)
 * @param ec_param1     Event count parameter 1
 * @param ec_param2     Event count parameter 2
 *
 * @return Negative (0xFF) if packet queued, 0 on error (socket full or closed)
 *
 * Original address: 0x00E1614E
 */
int8_t SOCK_$PUT(uint16_t sock_num, void **pkt_ptr, uint8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2);

 /*
  * SOCK_$EVENT_COUNTERS - Socket event counter array
  *
  * Array of pointers to event counters, indexed by socket number.
  *
  * Original address: 0xE28DB4
  */
extern ec_$eventcount_t *SOCK_$EVENT_COUNTERS[];

#endif /* SOCK_H */
