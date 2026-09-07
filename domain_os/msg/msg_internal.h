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
 * msg_$data_t - the MSG subsystem's global record at MSG_$DATA_BASE
 *
 * Recovered from MSG_$OPENI (0x00E591B4), MSG_$ALLOCATEI (0x00E592E6),
 * MSG_$CLOSEI (0x00E593E4) and MSG_$WAITI (0x00E59BC0), which are the only
 * writers.  All four establish A5 with "lea (0xe80d84).l,A5".
 *
 *   +0x1E   depth[]      "move.w (A3),(0x1e,A5,D1w*0x1)" with D1 = socket*2
 *                        (0x00E59276, 0x00E5936C).  Indexed by the socket
 *                        number itself, so slot 0 exists but is never used.
 *   +0x1D8  ownership    "lsl.w #0x3,D2w / lea (0x0,A5,D2w),A0 /
 *                        lea (0x1d8,A0),A1" (0x00E59202-0x00E5920A,
 *                        0x00E5935A-0x00E59360, 0x00E59420-0x00E59432,
 *                        0x00E59BEA-0x00E59BFC).  base + 0x1D8 + socket*8 is
 *                        a ONE-based array: the lowest address any caller can
 *                        reach is socket 1 at +0x1E0, which is exactly where
 *                        the depth table ends.
 *   +0x8E0  open_count   "addq.w #0x1,(0x8e0,A5)" (0x00E5927A, 0x00E59370),
 *                        "subq.w #0x1,(0x8e0,A5)" (0x00E5949E).
 *
 * Socket numbers run 1..0xE0: MSG_$OPENI rejects >= 0xE0 (0x00E591D2
 * "cmpi.w #0xe0,D0w / blt") while MSG_$CLOSEI and MSG_$WAITI accept 0xE0
 * (0x00E593FE / 0x00E59BDA "cmpi.w #0xe0,D0w / ble"), so the tables are sized
 * for socket 0xE0 as well.  depth therefore holds 0xE1 words (0x1E..0x1DF)
 * and ownership 0xE0 bitmaps (0x1E0..0x8DF).
 */
typedef struct msg_$data_t {
  uint8_t reserved_00[0x1E];             /* 0x000 */
  int16_t depth[MSG_MAX_SOCKET + 1];     /* 0x01E: indexed by socket */
  uint8_t ownership[MSG_MAX_SOCKET][8];  /* 0x1E0: indexed by socket - 1 */
  int16_t open_count;                    /* 0x8E0 */
} msg_$data_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(msg_$data_t, depth) == MSG_OFF_DEPTH_TABLE, "msg depth");
_Static_assert(offsetof(msg_$data_t, ownership) == MSG_OFF_OWNERSHIP + 8,
               "msg ownership starts one slot past the 1-based base");
_Static_assert(offsetof(msg_$data_t, open_count) == MSG_OFF_OPEN_COUNT,
               "msg open_count");
_Static_assert(sizeof(msg_$data_t) == 0x8E2, "msg_$data_t must be 0x8E2 bytes");
#endif

#if defined(ARCH_M68K)
#define MSG_$DATA ((msg_$data_t *)MSG_$DATA_BASE)
#else
extern msg_$data_t MSG_$DATA_STRUCT;
#define MSG_$DATA (&MSG_$DATA_STRUCT)
#endif

/*
 * MSG_$SOCK_OWNERS - the ONE-based spelling of msg_$data_t.ownership that the
 * original uses everywhere: base + 0x1D8 + socket*8, so MSG_$SOCK_OWNERS[n] is
 * socket n's 8-byte bitmap and slot 0 is never dereferenced.  It is the same
 * storage as MSG_$DATA->ownership[n - 1].
 *
 * Within a bitmap the byte index is (0x3F - asid) >> 3 in *word* arithmetic
 * with a logical shift, and the bit is asid & 7 - "btst.b D1,(0x0,A1,D0w*0x1)"
 * numbers bits modulo 8 (0x00E59C00, 0x00E59436).
 */
#define MSG_$SOCK_OWNERS                                                       \
  ((uint8_t(*)[8])((uint8_t *)MSG_$DATA + MSG_OFF_OWNERSHIP))

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
