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
  uint16_t pad_5a;         /* 0x5A: Padding */
  int16_t short_id;        /* 0x5C: Short packet ID counter (1-64000) */
  uint16_t pad_5e;         /* 0x5E: Padding */
  int32_t long_id;         /* 0x60: Long packet ID counter */

  /* Protocol configuration (offset 0x64) */
  uint16_t default_flags; /* 0x64: Default send flags */
  uint16_t pad_66;        /* 0x66: Padding */

  /* Ping request template (offset 0x68) */
  pkt_$request_template_t ping_template; /* 0x68: Ping request template */

  /* Ping server data (offset 0x88) */
  uint16_t ping_server_flags; /* 0x88: Ping server response flags */
} pkt_$data_t;

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
 */
extern int8_t NETWORK_$LOOPBACK_FLAG; /* Network loopback mode flag */
extern uint32_t *ROUTE_$PORTP;        /* Route port pointers */

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
