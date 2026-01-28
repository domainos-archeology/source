/*
 * MSG_$ Internal Definitions
 *
 * Internal data structures and helper functions for the MSG subsystem.
 */

#ifndef MSG_MSG_INTERNAL_H
#define MSG_MSG_INTERNAL_H

#include "ec/ec.h"
#include "misc/crash_system.h"
#include "ml/ml.h"
#include "msg/msg.h"
#include "network/network.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "route/route.h"
#include "sock/sock.h"

/*
 * MSG data base address
 * All MSG data structures are relative to this address.
 */
#define MSG_$DATA_BASE 0xE80D84

/*
 * MSG exclusion lock address
 */
#define MSG_$SOCK_LOCK 0xE242E4

/*
 * Data page addresses (for network message handling)
 */
#define MSG_$DPAGE_VA 0xE242F8 /* Data page virtual address */
#define MSG_$DPAGE_PA 0xE242FC /* Data page physical address */

/*
 * Offsets from MSG_$DATA_BASE
 */
#define MSG_OFF_DEPTH_TABLE 0x1E /* Socket depth table (2 bytes per socket) */
#define MSG_OFF_OWNERSHIP                                                      \
  0x1D8 /* Socket ownership bitmaps (8 bytes per socket) */
#define MSG_OFF_OPEN_COUNT 0x8E0 /* Count of open sockets */

/*
 * Socket ownership bitmap layout:
 * Each socket has 8 bytes (64 bits) for tracking ownership by up to 64 ASIDs.
 * Bit N is set if ASID N owns the socket.
 *
 * To check if ASID owns socket:
 *   bitmap_base = MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + (socket * 8)
 *   byte_index = (0x3F - ASID) >> 3
 *   bit_mask = 1 << (ASID & 7)
 *   owned = (bitmap[byte_index] & bit_mask) != 0
 */

/*
 * MSG_$DATA - MSG subsystem global data structure
 *
 * Layout at MSG_$DATA_BASE (0xE80D84):
 *   +0x00  : Reserved / header
 *   +0x1E  : Socket depth table (2 bytes per socket, up to 224 sockets)
 *   +0x1D8 : Socket ownership bitmaps (8 bytes per socket)
 *   +0x8E0 : Open socket count
 */
typedef struct msg_$data_s {
  uint8_t reserved[0x1E];
  int16_t depth[MSG_MAX_SOCKET];        /* Socket depth table */
  uint8_t ownership[MSG_MAX_SOCKET][8]; /* Ownership bitmaps */
  /* ... more fields at higher offsets ... */
} msg_$data_t;

/*
 * Check if current ASID owns the given socket
 */
static inline int msg_$check_ownership(msg_$socket_t socket) {
#if defined(ARCH_M68K)
  uint8_t *bitmap =
      (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + socket * 8);
  uint8_t asid = PROC1_$AS_ID;
  uint8_t byte_index = (0x3F - asid) >> 3;
  uint8_t bit_mask = 1 << (asid & 7);
  return (bitmap[byte_index] & bit_mask) != 0;
#else
  (void)socket;
  return 0;
#endif
}

/*
 * Set ownership bit for ASID on socket
 */
static inline void msg_$set_ownership(msg_$socket_t socket, uint8_t asid) {
#if defined(ARCH_M68K)
  uint8_t *bitmap =
      (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + socket * 8);
  uint8_t byte_index = (0x3F - asid) >> 3;
  uint8_t bit_mask = 1 << (asid & 7);
  bitmap[byte_index] |= bit_mask;
#else
  (void)socket;
  (void)asid;
#endif
}

/*
 * Clear ownership bit for ASID on socket
 */
static inline void msg_$clear_ownership(msg_$socket_t socket, uint8_t asid) {
#if defined(ARCH_M68K)
  uint8_t *bitmap =
      (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + socket * 8);
  uint8_t byte_index = (0x3F - asid) >> 3;
  uint8_t bit_mask = 1 << (asid & 7);
  bitmap[byte_index] &= ~bit_mask;
#else
  (void)socket;
  (void)asid;
#endif
}

/*
 * Get socket depth
 */
static inline int16_t msg_$get_depth(msg_$socket_t socket) {
#if defined(ARCH_M68K)
  return *(int16_t *)(MSG_$DATA_BASE + MSG_OFF_DEPTH_TABLE + socket * 2);
#else
  (void)socket;
  return 0;
#endif
}

/*
 * Set socket depth
 */
static inline void msg_$set_depth(msg_$socket_t socket, int16_t depth) {
#if defined(ARCH_M68K)
  *(int16_t *)(MSG_$DATA_BASE + MSG_OFF_DEPTH_TABLE + socket * 2) = depth;
#else
  (void)socket;
  (void)depth;
#endif
}

/*
 * Internal receive implementation
 */
extern void MSG_$$RCV_INTERNAL(int16_t socket, void *params,
                               status_$t *status_ret);

#endif /* MSG_MSG_INTERNAL_H */
