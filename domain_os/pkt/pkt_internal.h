/*
 * PKT - Packet Building Module (Internal)
 *
 * This module provides functions for building and managing network packet
 * headers for Domain/OS internet protocol communication.
 *
 * Internal data structures and global variables.
 */

#ifndef PKT_INTERNAL_H
#define PKT_INTERNAL_H

#include "app/app.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "ml/ml.h"
#include "net_io/net_io.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "os/os.h"
#include "pkt/pkt.h"
#include "proc1/proc1.h"
#include "rip/rip.h"
#include "route/route.h"
#include "sock/sock.h"
#include "time/time.h"

/*
 * Constants
 */
#define PKT_MAX_MISSING_NODES 10 /* Maximum tracked missing nodes */
#define PKT_MAX_SHORT_ID 64000   /* Maximum short packet ID before wrap */
#define PKT_CHUNK_SIZE 0x400     /* 1KB chunk size for data buffers */
#define PKT_MAX_DATA_CHUNKS 4    /* Maximum data buffer chunks */
#define PKT_MAX_HEADER 0x3B8     /* Maximum header size (952 bytes) */

#define PKT_PING_SOCKET 0x0D /* Socket number for ping service */

/*
 * Missing node tracking entry
 * Each entry tracks a node that failed to respond and when it was last seen
 * Size: 8 bytes
 */
typedef struct pkt_$missing_entry_t {
  uint32_t node_id;    /* 0x00: Node ID */
  uint32_t seq_number; /* 0x04: Visibility sequence number */
} pkt_$missing_entry_t;

/*
 * Packet request template for building requests
 * Size: varies by request type
 */
typedef struct pkt_$request_template_t {
  uint16_t type;        /* 0x00: Request type */
  uint16_t length;      /* 0x02: Data length */
  uint16_t id;          /* 0x04: Request ID */
  uint8_t flags;        /* 0x06: Flags */
  uint8_t protocol;     /* 0x07: Protocol number */
  uint16_t retry_count; /* 0x08: Retry count (0 = unlimited) */
  uint16_t pad_0a;      /* 0x0A: Padding */
  uint16_t field_0c;    /* 0x0C: Protocol-specific field */
                        /* Additional fields follow based on type */
} pkt_$request_template_t;

/*
 * PKT module global data area
 * Base address: 0xE24C9C (m68k)
 */
typedef struct pkt_$data_t {
  /* Missing node tracking (offset 0x00-0x4F) */
  pkt_$missing_entry_t
      missing_nodes[PKT_MAX_MISSING_NODES]; /* 0x00: Missing node entries */

  /* Synchronization (offset 0x50) */
  uint32_t spin_lock; /* 0x50: Spin lock for ID generation */

  /* Sequence/ID generation (offset 0x54-0x63) */
  uint32_t visibility_seq; /* 0x54: Visibility sequence counter */
  int16_t n_missing;       /* 0x58: Count of missing nodes */
  /*
   * 0x5A: the 2-byte request header both ping paths send.  Its address is
   * pushed as PKT_$SEND_INTERNET's "template" argument with a length of 2 by
   * PKT_$LIKELY_TO_ANSWER (0x00E12A9A "pea (0x5a,A5)" / 0x00E12A96
   * "move.w #0x2,-(SP)") and by PKT_$PING_SERVER (0x00E12D02 / 0x00E12CFE).
   * Image value at 0x00E24CF6 is 0x0001.
   */
  uint16_t ping_req_hdr;
  int16_t short_id;        /* 0x5C: Short packet ID counter (1-64000) */
  uint16_t pad_5e;         /* 0x5E: Padding */
  int32_t long_id;         /* 0x60: Long packet ID counter */

  /* Protocol configuration (offset 0x64) */
  uint16_t default_flags; /* 0x64: Default send flags */
  uint16_t pad_66;        /* 0x66: Padding */

  /* Ping request template (offset 0x68) */
  pkt_$request_template_t ping_template; /* 0x68: Ping request template */
  /* TODO(source-wv5s): 0x76-0x87 not yet identified */
  uint8_t reserved_76[0x12];

  /*
   * 0x88: the packet-info record PKT_$PING_SERVER hands to
   * PKT_$SEND_INTERNET.  The reply flags are stored into its first word
   * (0x00E12CE6 "move.w D4w,(0x88,A5)") and its address is then pushed
   * (0x00E12D0A "pea (0x88,A5)").
   */
  uint16_t ping_server_flags;
} pkt_$data_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(pkt_$data_t, spin_lock) == 0x50, "pkt_$data_t.spin_lock");
_Static_assert(offsetof(pkt_$data_t, n_missing) == 0x58, "pkt_$data_t.n_missing");
_Static_assert(offsetof(pkt_$data_t, ping_req_hdr) == 0x5A, "pkt_$data_t.ping_req_hdr");
_Static_assert(offsetof(pkt_$data_t, short_id) == 0x5C, "pkt_$data_t.short_id");
_Static_assert(offsetof(pkt_$data_t, long_id) == 0x60, "pkt_$data_t.long_id");
_Static_assert(offsetof(pkt_$data_t, default_flags) == 0x64, "pkt_$data_t.default_flags");
_Static_assert(offsetof(pkt_$data_t, ping_template) == 0x68, "pkt_$data_t.ping_template");
_Static_assert(offsetof(pkt_$data_t, ping_server_flags) == 0x88, "pkt_$data_t.ping_server_flags");
#endif

/*
 * pkt_$no_data - the all-zero longword at 0x00E12BB4, in the code region
 * between PKT_$LIKELY_TO_ANSWER's rts and PKT_$PING_SERVER.
 *
 * Both ping senders pass its address as PKT_$SEND_INTERNET's "data" argument
 * with a data length of zero, so the buffer is never read:
 *   0x00E12A92  pea (0x120,PC)     0x00E12A92 + 2 + 0x120 = 0x00E12BB4
 *   0x00E12CFA  pea (-0x148,PC)    0x00E12CFA + 2 - 0x148 = 0x00E12BB4
 */
static const uint32_t pkt_$no_data = 0;

/*
 * pkt_$internet_hdr_t - the fields PKT reads out of a received internet
 * packet header (the pointer APP_$RECEIVE leaves in pkt_$recv_result_t.hdr).
 */
typedef struct pkt_$internet_hdr_t {
  uint16_t reserved_00;    /* 0x00 */
  uint16_t hdr_len;        /* 0x02: request header length (0x00E12C8E) */
  uint16_t data_len;       /* 0x04: data length (0x00E12B02, 0x00E12C6E) */
  int16_t request_id;      /* 0x06: request id (0x00E12B0C, 0x00E12C86) */
  uint8_t reserved_08[6];  /* 0x08 */
  uint32_t src_node;       /* 0x0E: source node (0x00E12C74) */
  uint16_t src_sock;       /* 0x12: source socket (0x00E12C7A) */
  uint8_t flags;           /* 0x14: request flags (0x00E12C82) */
} __attribute__((packed)) pkt_$internet_hdr_t;

/*
 * pkt_$recv_result_t - the 44-byte record APP_$RECEIVE fills in (app.h
 * documents the size).  PKT reads only these fields:
 *   0x00  hdr        header VA        (0x00E12AFE, 0x00E12C6A)
 *   0x04  hdr_ppn    header page      (0x00E12B08, 0x00E12CB0)
 *   0x08  data_bufs  data buffers     (0x00E12B24, 0x00E12CC8)
 *   0x1C  routing_key                 (0x00E12C64)
 */
typedef struct pkt_$recv_result_t {
  pkt_$internet_hdr_t *hdr;               /* 0x00 */
  uint32_t hdr_ppn;                       /* 0x04 */
  uint32_t data_bufs[PKT_MAX_DATA_CHUNKS];/* 0x08 */
  uint32_t reserved_18;                   /* 0x18 */
  uint32_t routing_key;                   /* 0x1C */
  uint32_t reserved_20[3];                /* 0x20 */
} pkt_$recv_result_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(pkt_$internet_hdr_t, request_id) == 0x06, "pkt_$internet_hdr_t.request_id");
_Static_assert(offsetof(pkt_$internet_hdr_t, flags) == 0x14, "pkt_$internet_hdr_t.flags");
_Static_assert(offsetof(pkt_$recv_result_t, data_bufs) == 0x08, "pkt_$recv_result_t.data_bufs");
_Static_assert(offsetof(pkt_$recv_result_t, routing_key) == 0x1C, "pkt_$recv_result_t.routing_key");
_Static_assert(sizeof(pkt_$recv_result_t) == 44, "pkt_$recv_result_t must be 44 bytes");
#endif

/*
 * Global data references (m68k addresses)
 */
#if defined(ARCH_M68K)
#define PKT_$DATA ((pkt_$data_t *)0xE24C9C)
#define PKT_$N_MISSING (PKT_$DATA->n_missing)
#else
extern pkt_$data_t PKT_$DATA_STRUCT;
#define PKT_$DATA (&PKT_$DATA_STRUCT)
#define PKT_$N_MISSING (PKT_$DATA->n_missing)
#endif

/*
 * External references needed by PKT
 *
 * ROUTE_$PORTP (0x00E26EE8) is the array of route_$port_t pointers declared by
 * route/route.h; PKT_$LIKELY_TO_ANSWER indexes it with the port number that
 * RIP_$FIND_NEXTHOP returned (0x00E129FA - 0x00E12A08).
 */
extern int8_t NETWORK_$LOOPBACK_FLAG; /* Network loopback mode flag */

/*
 * Internal function prototypes
 */

/*
 * PKT_$PING_SERVER - Ping server process entry point
 *
 * This is the main loop for the ping server process that responds
 * to network ping requests.
 *
 * Original address: 0x00E12BB8
 */
void PKT_$PING_SERVER(void);

#endif /* PKT_INTERNAL_H */
