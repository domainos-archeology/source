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
 * MSG_$SOCK_OWNERS - per-socket ownership bitmaps, 8 bytes (64 ASID bits) each
 *
 * 0x00E80F5C = MSG_$DATA_BASE + MSG_OFF_OWNERSHIP.  MSG_$WAITI addresses it as
 * base + socket*8 + byte_index (0x00E59BEA "lsl.w #3,D0w" then 0x00E59BFC
 * "lea (0x1D8,A1),A1"), so slot 0 is unused and MSG_$SOCK_OWNERS[sock] is the
 * bitmap for socket "sock".  Within a bitmap the byte is (0x3F - asid) >> 3
 * and the bit is asid & 7 (0x00E59C00 "btst.b D1,(0x0,A1,D0w*0x1)").
 */
extern uint8_t MSG_$SOCK_OWNERS[][8];

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
 *
 * TODO(source-eq3o): this and the four accessors below are unused, are
 * compiled out on non-m68k hosts, and duplicate MSG_$SOCK_OWNERS above.
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
 * Internal receive implementation (0x00E59548)
 */
void MSG_$$RCV_INTERNAL(int16_t socket, void *params, status_$t *status_ret);

/*
 * NETWORK_$SET_SERVICE operation codes used by MSG.
 *
 * In the original these are PC-relative words in the code segment whose
 * addresses are pushed as the op_ptr argument:
 *   MSG_$NET_SERVICE       0x00E592C8 = 0 (NETWORK_OP_OR_BITS)     - MSG_$OPENI,
 *                                                                    MSG_$ALLOCATE
 *   MSG_$NET_SERVICE_CLOSE 0x00E594F2 = 1 (NETWORK_OP_AND_NOT_BITS) - MSG_$CLOSEI
 */
static const int16_t MSG_$NET_SERVICE = 0;
static const int16_t MSG_$NET_SERVICE_CLOSE = 1;

/*
 * MSG_$SAR_TIMEOUT - Longword constant (0xFFFFFFFF) at 0x00E59DD0 whose
 * address MSG_$SAR passes as the second argument of MSG_$SARI
 * (pea (0x18,PC) at 0x00E59DB6).
 */
static const int32_t MSG_$SAR_TIMEOUT = -1;

#endif /* MSG_MSG_INTERNAL_H */
