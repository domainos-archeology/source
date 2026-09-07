/*
 * ASKNODE - Internal Header
 *
 * Internal types and helper functions for the ASKNODE subsystem.
 * This header should only be included by ASKNODE implementation files.
 */

#ifndef ASKNODE_INTERNAL_H
#define ASKNODE_INTERNAL_H

#include "app/app.h"
#include "asknode/asknode.h"
#include "cal/cal.h"
#include "dir/dir.h"
#include "disk/disk.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "gpu/gpu.h"
#include "hint/hint.h"
#include "log/log.h"
#include "mem/mem.h"
#include "misc/misc.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "name/name.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "os/os.h"
#include "pkt/pkt.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "prom/prom.h"
#include "ring/ring.h"
#include "rip/rip.h"
#include "route/route.h"
#include "sock/sock.h"
#include "time/time.h"
#include "volx/volx.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* Socket 5 is used for WHO_REMOTE queries */
#define ASKNODE_WHO_SOCKET 5

/* Packet socket type */
#define ASKNODE_PKT_TYPE 4

/* Default wait timeout in clock ticks */
#define ASKNODE_DEFAULT_TIMEOUT 6

/* Response buffer sizes */
#define ASKNODE_MAX_RESPONSE_LEN 0x200
#define ASKNODE_REQUEST_LEN 0x18

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/*
 * asknode_request_t - the 0x18-byte request record ASKNODE_$SERVER copies out
 * of the received packet (0x00E659EE-0x00E65A0E; the copy length is
 * min(0x18, reply->length)).
 *
 * It is a Pascal variant record - the request types overlay different shapes
 * on the same words - so the longword fields below are read in halves at some
 * sites.  Every such overlay is spelled with shifts in asknode/server.c so
 * that it works on a little-endian host too:
 *
 *   node_id  +0x04: request 0x31 reads the HIGH word as a control word
 *                   ("move.w (-0x264,A6),D1w" at 0x00E65D56) and the LOW word
 *                   as the LOG_$READ2 offset ("move.w (-0x262,A6)" at
 *                   0x00E65D66)
 *   param1   +0x08: request 0x00 uses the HIGH word as a hop counter
 *                   ("move.w (-0x260,A6),(-0x242,A6)" at 0x00E65B28 and
 *                   "subq.w #0x1,(-0x260,A6)" at 0x00E65B32) while request
 *                   0x2D reads the whole longword (0x00E65BC2, 0x00E65C38)
 */
typedef struct asknode_request_t {
  uint16_t version;      /* 0x00: Protocol version (2 or 3) */
  uint16_t request_type; /* 0x02: Request type code, the jump-table index */
  uint32_t node_id;      /* 0x04: Target node id / request 0x31's log control */
  uint32_t param1;       /* 0x08 */
  uint32_t param2;       /* 0x0C */
  int8_t   forwarded;    /* 0x10: Pascal boolean - "tst.b (-0x258,A6) / bmi"
                          *       at 0x00E65BB0, cleared at 0x00E65E5C */
  int8_t   _pad_11;      /* 0x11 */
  int16_t  count;        /* 0x12: hop counter for request 0x2D (0x00E65C32,
                          *       0x00E65C42) */
  uint32_t param3;       /* 0x14 */
} asknode_request_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(asknode_request_t, version) == 0x00, "asknode_request_t.version");
_Static_assert(__builtin_offsetof(asknode_request_t, request_type) == 0x02, "asknode_request_t.request_type");
_Static_assert(__builtin_offsetof(asknode_request_t, _pad_11) == 0x11, "asknode_request_t._pad_11");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(asknode_request_t, node_id)   == 0x04, "asknode_request_t.node_id");
_Static_assert(offsetof(asknode_request_t, param1)    == 0x08, "asknode_request_t.param1");
_Static_assert(offsetof(asknode_request_t, param2)    == 0x0C, "asknode_request_t.param2");
_Static_assert(offsetof(asknode_request_t, forwarded) == 0x10, "asknode_request_t.forwarded");
_Static_assert(offsetof(asknode_request_t, count)     == 0x12, "asknode_request_t.count");
_Static_assert(offsetof(asknode_request_t, param3)    == 0x14, "asknode_request_t.param3");
_Static_assert(sizeof(asknode_request_t) == 0x18, "asknode_request_t must be 0x18 bytes");
#endif

/*
 * asknode_response_t - Network response structure
 *
 * Structure for ASKNODE network responses.
 */
typedef struct asknode_response_t {
  uint16_t version;       /* 0x00: Protocol version */
  uint16_t response_type; /* 0x02: Response type */
  status_$t status;       /* 0x04: Status code - stored with a single move.l
                           *       (ASKNODE_$SERVER 0x00E65B6E / 0x00E65B78,
                           *       tested by ASKNODE_$WHO_NOTOPO at
                           *       0x00E662CA as tst.l (rec+0x04)) */
  uint32_t node_id;       /* 0x08: Responding node ID (move.l NODE_$ME at
                           *       0x00E65B20) */
  uint16_t flags;         /* 0x0C: Response flags (0xB1FF at 0x00E65B3E) */
  int16_t count;          /* 0x0E: Count remaining (0x00E65B28) */
                          /* Response data follows */
} asknode_response_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(asknode_response_t, version) == 0x00, "asknode_response_t.version");
_Static_assert(__builtin_offsetof(asknode_response_t, response_type) == 0x02, "asknode_response_t.response_type");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(asknode_response_t, status)  == 0x04, "asknode_response_t.status");
_Static_assert(offsetof(asknode_response_t, node_id) == 0x08, "asknode_response_t.node_id");
_Static_assert(offsetof(asknode_response_t, flags)   == 0x0C, "asknode_response_t.flags");
_Static_assert(offsetof(asknode_response_t, count)   == 0x0E, "asknode_response_t.count");
_Static_assert(sizeof(asknode_response_t) == 0x10, "asknode_response_t must be 16 bytes");
#endif

/*
 * asknode_$reply_hdr_t - what app_$receive_rec_t.reply points at
 *
 * ASKNODE_$WHO_NOTOPO reads it through A0 = record->reply
 * (0x00E66258-0x00E66280); APP_$RECEIVE synthesises the same fields on the
 * Domain-internet path (0x00E00980-0x00E009AC).
 */
typedef struct asknode_$reply_hdr_t {
  uint16_t magic;         /* 0x00: 0x0118 (0x00E00984) */
  uint16_t length;        /* 0x02: reply byte count, clamped to 0x200 by
                           *       ASKNODE_$WHO_NOTOPO (0x00E66270) */
  uint16_t data_len;      /* 0x04: payload byte count handed to
                           *       PKT_$DUMP_DATA (saved at 0x00E6625C,
                           *       pushed at 0x00E662A8) */
  int16_t  reply_id;      /* 0x06: matched against the request id
                           *       (0x00E66280 / 0x00E662C6) */
  uint32_t sender_node;   /* 0x08: NODE_$ME of the sender (0x00E009C6) */
  uint16_t f0c;           /* 0x0C */
  uint32_t node_id;       /* 0x0E: the responding node id
                           *       (0x00E6626A, UNALIGNED longword) */
  uint16_t src_socket;    /* 0x12: the socket the packet came in on;
                           *       ASKNODE_$SERVER reuses it as the reply's
                           *       destination socket (0x00E659DC,
                           *       0x00E65E1C) */
  uint8_t  f14;           /* 0x14: request flags; ASKNODE_$SERVER tests bit 2
                           *       ("btst.l #0x2,D4" at 0x00E65B98) */
  uint8_t  _pad_15;       /* 0x15 */
} __attribute__((packed)) asknode_$reply_hdr_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(asknode_$reply_hdr_t, data_len)    == 0x04, "reply_hdr.data_len");
_Static_assert(offsetof(asknode_$reply_hdr_t, reply_id)    == 0x06, "reply_hdr.reply_id");
_Static_assert(offsetof(asknode_$reply_hdr_t, sender_node) == 0x08, "reply_hdr.sender_node");
_Static_assert(offsetof(asknode_$reply_hdr_t, node_id)     == 0x0E, "reply_hdr.node_id");
_Static_assert(offsetof(asknode_$reply_hdr_t, src_socket)  == 0x12, "reply_hdr.src_socket");
_Static_assert(offsetof(asknode_$reply_hdr_t, f14)         == 0x14, "reply_hdr.f14");
#endif

/*
 * asknode_$server_ctx_t - the record ASKNODE_$SERVER's caller passes as its
 * first argument (A2), 0x22 bytes.
 *
 * This is NOT the reply buffer: the reply ASKNODE_$SERVER transmits is an
 * asknode_response_t built on its own stack at A6-0x250.  The context record
 * is what the server hands back to its caller so that a WHO query can be
 * propagated: on the way out ASKNODE_$SERVER copies the request's version
 * and its 20 bytes from +0x04 into it (0x00E65E60 - 0x00E65E72) and appends
 * the source port and request id (0x00E65E76 / 0x00E65E7C).  Fields +0x1C
 * and +0x1E are scratch that individual request types use: request 0x45
 * reads TIME_$CLOCK into +0x1C, zeroes the top word and leaves the
 * time-difference longword at +0x1E (0x00E65CA2 - 0x00E65CCA).
 */
typedef struct asknode_$server_ctx_t {
  uint16_t version;       /* 0x00 */
  uint16_t request_type;  /* 0x02 */
  uint32_t node_id;       /* 0x04 */
  uint32_t param1;        /* 0x08 */
  uint32_t param2;        /* 0x0C */
  int8_t   forwarded;     /* 0x10: mirrors asknode_request_t.forwarded */
  int8_t   _pad_11;       /* 0x11 */
  int16_t  count;         /* 0x12: mirrors asknode_request_t.count */
  uint32_t param3;        /* 0x14 */
  int16_t  request_id;    /* 0x18: the request id, taken from the received
                           *       reply header's +0x06 ("move.w (-0x29a,A6),
                           *       (0x18,A2)" at 0x00E65E76).
                           *       ASKNODE_$PROPAGATE_WHO passes it as
                           *       PKT_$SEND_INTERNET's request_id
                           *       (0x00E65F04). */
  uint16_t socket;        /* 0x1A: the socket the request arrived on, from the
                           *       reply header's +0x12 ("move.w (-0x29c,A6),
                           *       (0x1a,A2)" at 0x00E65E7C).
                           *       ASKNODE_$PROPAGATE_WHO uses it as the
                           *       destination socket (0x00E65F18) or the
                           *       source socket (0x00E65F4E). */
  uint16_t clock_hi;      /* 0x1C */
  uint32_t clock_lo;      /* 0x1E */
} __attribute__((packed)) asknode_$server_ctx_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(asknode_$server_ctx_t, param3)     == 0x14, "server_ctx.param3");
_Static_assert(offsetof(asknode_$server_ctx_t, request_id) == 0x18, "server_ctx.request_id");
_Static_assert(offsetof(asknode_$server_ctx_t, socket)     == 0x1A, "server_ctx.socket");
_Static_assert(offsetof(asknode_$server_ctx_t, clock_hi)   == 0x1C, "server_ctx.clock_hi");
_Static_assert(offsetof(asknode_$server_ctx_t, clock_lo)   == 0x1E, "server_ctx.clock_lo");
_Static_assert(sizeof(asknode_$server_ctx_t) == 0x22, "asknode_$server_ctx_t must be 0x22 bytes");
#endif

/*
 * asknode_who_response_t - WHO response structure
 *
 * Extended response for WHO queries.
 */
typedef struct asknode_who_response_t {
  uint16_t version;       /* 0x00: Protocol version (3) */
  uint16_t response_type; /* 0x02: Response type (0x2E or 1) */
  status_$t status;       /* 0x04: Status code (see asknode_response_t) */
  uint32_t node_id;       /* 0x08: Responding node ID */
  uint16_t flags;         /* 0x0C: Response flags (0xB1FF) */
  int16_t count;          /* 0x0E: Count remaining */
  uint32_t time_high;     /* 0x10: Time high word */
  uint32_t time_low;      /* 0x14: Time low word */
} asknode_who_response_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(asknode_who_response_t, version) == 0x00, "asknode_who_response_t.version");
_Static_assert(__builtin_offsetof(asknode_who_response_t, response_type) == 0x02, "asknode_who_response_t.response_type");
_Static_assert(__builtin_offsetof(asknode_who_response_t, flags) == 0x0C, "asknode_who_response_t.flags");
_Static_assert(__builtin_offsetof(asknode_who_response_t, count) == 0x0E, "asknode_who_response_t.count");
_Static_assert(__builtin_offsetof(asknode_who_response_t, time_high) == 0x10, "asknode_who_response_t.time_high");
_Static_assert(__builtin_offsetof(asknode_who_response_t, time_low) == 0x14, "asknode_who_response_t.time_low");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(asknode_who_response_t, status)  == 0x04, "who_response.status");
_Static_assert(offsetof(asknode_who_response_t, node_id) == 0x08, "who_response.node_id");
#endif

/*
 * ============================================================================
 * External References
 * ============================================================================
 *
 * Network globals (NETWORK_$FAILURE_REC, NETWORK_$ACTIVITY_FLAG,
 * NETWORK_$DISKLESS, NETWORK_$MOTHER_NODE, the NETWORK_$*_CNT statistics,
 * NETWORK_$CAPABLE_FLAGS) come from network/network.h; MEM_$MEM_REC from
 * mem/mem.h; MMAP_$REAL_PAGES from mmap/mmap.h; PROM_$MACHINE_ID from
 * prom/prom.h; MMU_$SYSTEM_REV from mmu/mmu.h; GPU_$PRESENT from gpu/gpu.h;
 * RING_$CTL from ring/ring.h.
 */

/* Packet info template at 0x00E82408 - default values for PKT_$SEND_INTERNET */
/* PKT_$DEFAULT_INFO is exported from pkt/pkt.h (bead source-3uo). */

/*
 * Protocol version at 0x00E82426 - determines WHO request version
 * When == 3, use protocol version 2; otherwise use version 3
 */
extern uint16_t ASKNODE_$PROTOCOL_VERSION;

/*
 * Empty data placeholder at 0x00E658CC (a zero longword in the code segment)
 * - used as "no data" in network sends and as a -1/0 request length.
 */
extern uint32_t ASKNODE_$EMPTY_DATA;

/*
 * The socket event counts used by WHO_NOTOPO / WHO_REMOTE (sock_spinlock at
 * 0x00E28DB0 + sock_num * 4, SOCK_$EC_5 at 0x00E28DC4) are entries of the
 * socket pointer table: SOCK_$EVENT_COUNTERS[sock_num - 1] (sock/sock.h).
 */

#endif /* ASKNODE_INTERNAL_H */
