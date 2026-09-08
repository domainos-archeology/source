/*
 * MSG_$ Internal Definitions
 *
 * Internal data structures and helper functions for the MSG subsystem.
 */

#ifndef MSG_MSG_INTERNAL_H
#define MSG_MSG_INTERNAL_H

#include "ec/ec.h"
#include "net_io/net_io.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "misc/crash_system.h"
#include "ml/ml.h"
#include "app/app.h"   /* app_$reply_hdr_t */
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
 * MSG network-send record, base 0xE242E4
 *
 * MSG_$$SEND establishes it as its module base ("lea (0xe242e4).l,A5" at
 * 0x00E0D9F4) and reaches the data page through it: (0x14,A5) is the page's
 * virtual address, (0x18,A5) its physical address and (0x1C,A5) the in-use
 * counter it bumps and drops (0x00E0DBE6, 0x00E0DC1E, 0x00E0DCA6).
 * MSG_$OPENI, MSG_$ALLOCATEI, MSG_$CLOSEI, MSG_$SHARE_SOCKET and MSG_$FORK
 * pass the base itself to ML_$EXCLUSION_START / ML_$EXCLUSION_STOP
 * (0x00E591F2, 0x00E59410, 0x00E73F12), so the first 0x14 bytes are an
 * ml_$exclusion_t.
 *
 * The lock and the data page are declared separately rather than as one
 * record because sizeof(ml_$exclusion_t) is 0x12 on m68k but 0x20 on a
 * 64-bit host, which would move every following field.
 */
#define MSG_$SOCK_LOCK_ADDR 0xE242E4    /* ml_$exclusion_t */
#define MSG_$DPAGE_ADDR     0xE242F8    /* msg_$dpage_t (= lock base + 0x14) */

/*
 * msg_$dpage_t - the single bounce page MSG_$$SEND uses for short remote
 * payloads instead of allocating netbuf pages.
 */
typedef struct msg_$dpage_t {
  uint32_t va;   /* 0x00 (0xE242F8): virtual address  (0x00E0DBF8) */
  uint32_t pa;   /* 0x04 (0xE242FC): physical address (0x00E0DBFE) */
  int16_t in_use;/* 0x08 (0xE24300): pre-increment claim counter; 0 after the
                  *      increment means the caller owns the page
                  *      (0x00E0DBE6 "addq.w #0x1,(0x1c,A5)" /
                  *       0x00E0DBEA "tst.w (0x1c,A5) / bne") */
} msg_$dpage_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(msg_$dpage_t, pa) == 0x04, "msg_$dpage_t.pa");
_Static_assert(offsetof(msg_$dpage_t, in_use) == 0x08, "msg_$dpage_t.in_use");
#endif

#if defined(ARCH_M68K)
#define MSG_$SOCK_LOCK ((ml_$exclusion_t *)MSG_$SOCK_LOCK_ADDR)
#define MSG_$DPAGE     ((msg_$dpage_t *)MSG_$DPAGE_ADDR)
#else
extern ml_$exclusion_t MSG_$SOCK_LOCK_STRUCT;
extern msg_$dpage_t MSG_$DPAGE_STRUCT;
#define MSG_$SOCK_LOCK (&MSG_$SOCK_LOCK_STRUCT)
#define MSG_$DPAGE     (&MSG_$DPAGE_STRUCT)
#endif

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
  /*
   * 0x000: the 30-byte pkt_$info_t template MSG_$SEND copies onto its stack
   * before overwriting the flags word ("lea (A5),A2 / lea (-0x20,A6),A3 /
   * moveq #0x6 / move.l (A2)+,(A3)+ / dbf / move.w (A2)+,(A3)+" at
   * 0x00E59A40-0x00E59A4E).  Only 30 of pkt_$info_t's 32 bytes are copied.
   */
  uint8_t send_template[0x1E];           /* 0x000 */
  int16_t depth[MSG_MAX_SOCKET + 1];     /* 0x01E: indexed by socket */
  uint8_t ownership[MSG_MAX_SOCKET][8];  /* 0x1E0: indexed by socket - 1 */
  int16_t open_count;                    /* 0x8E0 */
} msg_$data_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(msg_$data_t, send_template) == 0, "msg send_template");
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
 * MSG_$$SEND - the shared body behind MSG_$SEND and MSG_$SENDI (0x00E0D9EC).
 * Fifteen arguments at 0x08, 0x0A, 0x0E, 0x12, 0x14, 0x18, 0x1C, 0x1E, 0x22,
 * 0x24, 0x28, 0x2A, 0x2E, 0x30, 0x34.  A Pascal procedure: it leaves no
 * result, both callers read the answer out of send_info / status_ret.
 */
void MSG_$$SEND(int16_t port_num, uint32_t routing_key, uint32_t dest_node,
                uint16_t dest_sock, int32_t src_node_or, uint32_t src_node,
                uint16_t src_sock, const pkt_$info_t *pkt_info,
                uint16_t request_id, void *template, uint16_t template_len,
                void *data, uint16_t data_len,
                net_io_$send_info_t *send_info, status_$t *status_ret);

/*
 * msg_$reply_hdr_t - the application reply record APP_$RECEIVE points
 * app_$receive_rec_t.reply at, as read by the MSG receive path.
 *
 * Offsets recovered from MSG_$$RCV_INTERNAL (0x00E59548) and the identical
 * inline copy in MSG_$RCV_CONTIGI (0x00E597A6); MSG_$$SEND builds the same
 * record on the way out.  The longword at 0x0E is on an odd longword
 * boundary, so the record must be packed for a host build to agree with
 * m68k's 2-byte alignment.
 */
typedef struct msg_$reply_hdr_t {
  /*
   * 0x00..0x07: the shared APP reply prefix (app/app.h).  MSG reads
   *   prefix.template_len  0x00E5961C `cmp.w D2w` / `move.w (0x2,A2)`;
   *                        decremented by 0x10 when the 16-byte internet
   *                        address is consumed (0x00E59612)
   *   prefix.data_len      0x00E59666 / 0x00E5968A
   *   prefix.request_id    0x00E595C0 - MSG's "message type"
   * and never reads prefix.magic.
   */
  app_$reply_hdr_t prefix;
  uint32_t dest_node;     /* 0x08: 0x00E595A0 */
  uint16_t dest_sock;     /* 0x0C: 0x00E595A8 */
  uint32_t src_node;      /* 0x0E: 0x00E595B0 (unaligned longword) */
  uint16_t src_sock;      /* 0x12: 0x00E595B8 */
  uint8_t  proto_family;  /* 0x14: 0x00E595C6 */
  uint8_t  proto_type;    /* 0x15: 0x00E595DC */
  uint8_t  proto_subtype; /* 0x16: 0x00E595E6 */
} __attribute__((packed)) msg_$reply_hdr_t;

_Static_assert(offsetof(msg_$reply_hdr_t, prefix) == 0x00, "msg reply.prefix");
_Static_assert(sizeof(app_$reply_hdr_t) == 0x08, "msg reply prefix size");
_Static_assert(offsetof(msg_$reply_hdr_t, dest_node) == 0x08, "msg reply.dest_node");
_Static_assert(offsetof(msg_$reply_hdr_t, dest_sock) == 0x0C, "msg reply.dest_sock");
_Static_assert(offsetof(msg_$reply_hdr_t, src_node) == 0x0E, "msg reply.src_node");
_Static_assert(offsetof(msg_$reply_hdr_t, src_sock) == 0x12, "msg reply.src_sock");
_Static_assert(offsetof(msg_$reply_hdr_t, proto_family) == 0x14, "msg reply.proto_family");
_Static_assert(offsetof(msg_$reply_hdr_t, proto_type) == 0x15, "msg reply.proto_type");
_Static_assert(offsetof(msg_$reply_hdr_t, proto_subtype) == 0x16, "msg reply.proto_subtype");

/*
 * The proto_type / proto_subtype pair that makes MSG_$$RCV_INTERNAL peel a
 * 16-byte internet address off the front of the template
 * (0x00E595F6 `cmpi.w #0x2` / 0x00E595FC `cmpi.w #0x29`).
 */
#define MSG_PROTO_TYPE_INET     0x02
#define MSG_PROTO_SUBTYPE_INET  0x29

/*
 * msg_$hw_addr_t.flags is the socket queue depth, extracted from
 * app_$receive_rec_t.flags_lo with "move.w #0x7f80,D5w / and.w (-0x8,A6),D5w
 * / lsr.w #0x7,D5w" (0x00E595CC).
 */
#define MSG_HW_FLAGS_MASK   0x7F80
#define MSG_HW_FLAGS_SHIFT  7

/*
 * MSG_$$RCV_INTERNAL - the shared body behind MSG_$RCVI (0x00E59548).
 *
 * EIGHTEEN arguments; the prologue and body read them at 0x08 socket(w),
 * 0x0A dest_net, 0x0E dest_node, 0x12 dest_sock, 0x16 src_net, 0x1A
 * src_node, 0x1E src_sock, 0x22 hw_addr, 0x26 msg_type, 0x2A template,
 * 0x2E template_max(w), 0x30 template_len_ret, 0x34 data, 0x38 data_max(w),
 * 0x3A data_len_ret, 0x3E ec_param1_ret, 0x42 ec_param2_ret, 0x46 status.
 * Callers reserve a 2-byte Pascal result slot they never read
 * (0x00E596FE `subq.l #0x2,SP`, no pop - the unlk cleans up).
 */
void MSG_$$RCV_INTERNAL(uint16_t socket,
                        uint32_t *dest_net, uint32_t *dest_node,
                        uint16_t *dest_sock,
                        uint32_t *src_net, uint32_t *src_node,
                        uint16_t *src_sock,
                        msg_$hw_addr_t *hw_addr, uint16_t *msg_type,
                        void *template, uint16_t template_max,
                        uint16_t *template_len_ret,
                        void *data, uint16_t data_max,
                        uint16_t *data_len_ret,
                        uint16_t *ec_param1_ret, uint16_t *ec_param2_ret,
                        status_$t *status_ret);

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
