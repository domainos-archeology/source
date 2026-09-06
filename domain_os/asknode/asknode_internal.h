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
 * asknode_request_t - Network request structure
 *
 * Structure for ASKNODE network requests.
 */
typedef struct asknode_request_t {
  uint16_t version;      /* 0x00: Protocol version (2 or 3) */
  uint16_t request_type; /* 0x02: Request type code */
  uint32_t node_id;      /* 0x04: Target node ID */
  uint32_t param1;       /* 0x08: First parameter */
  uint32_t param2;       /* 0x0C: Second parameter */
  int16_t count;         /* 0x10: Count/size field */
  int8_t flags;          /* 0x12: Request flags */
  int8_t pad;            /* 0x13: Padding */
  uint32_t param3;       /* 0x14: Third parameter */
} asknode_request_t;

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

#if defined(ARCH_M68K)
_Static_assert(offsetof(asknode_response_t, status)  == 0x04, "asknode_response_t.status");
_Static_assert(offsetof(asknode_response_t, node_id) == 0x08, "asknode_response_t.node_id");
_Static_assert(offsetof(asknode_response_t, flags)   == 0x0C, "asknode_response_t.flags");
_Static_assert(offsetof(asknode_response_t, count)   == 0x0E, "asknode_response_t.count");
_Static_assert(sizeof(asknode_response_t) == 0x10, "asknode_response_t must be 16 bytes");
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
  int16_t  count;         /* 0x10 */
  int8_t   flags;         /* 0x12 */
  int8_t   pad;           /* 0x13 */
  uint32_t param3;        /* 0x14 */
  uint16_t src_port;      /* 0x18 */
  int16_t  request_id;    /* 0x1A */
  uint16_t clock_hi;      /* 0x1C */
  uint32_t clock_lo;      /* 0x1E */
} __attribute__((packed)) asknode_$server_ctx_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(asknode_$server_ctx_t, param3)     == 0x14, "server_ctx.param3");
_Static_assert(offsetof(asknode_$server_ctx_t, src_port)   == 0x18, "server_ctx.src_port");
_Static_assert(offsetof(asknode_$server_ctx_t, request_id) == 0x1A, "server_ctx.request_id");
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
 * mem/mem.h; MMAP_$REAL_PAGES from mmap/mmap.h; PROM_$SAU_AND_AUX from
 * prom/prom.h; MMU_$SYSTEM_REV from mmu/mmu.h; GPU_$PRESENT from gpu/gpu.h;
 * RING_$DATA from ring/ring.h.
 */

/* Packet info template at 0x00E82408 - default values for PKT_$SEND_INTERNET */
extern uint32_t PKT_$DEFAULT_INFO[8];

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
