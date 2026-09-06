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
  uint32_t node_id;       /* 0x04: Responding node ID */
  status_$t status;       /* 0x08: Status code */
  uint16_t flags;         /* 0x0C: Response flags */
  int16_t count;          /* 0x0E: Count remaining */
                          /* Response data follows */
} asknode_response_t;

/*
 * asknode_who_response_t - WHO response structure
 *
 * Extended response for WHO queries.
 */
typedef struct asknode_who_response_t {
  uint16_t version;       /* 0x00: Protocol version (3) */
  uint16_t response_type; /* 0x02: Response type (0x2E or 1) */
  uint32_t node_id;       /* 0x04: Responding node ID */
  status_$t status;       /* 0x08: Status code */
  uint16_t flags;         /* 0x0C: Response flags (0xB1FF) */
  int16_t count;          /* 0x0E: Count remaining */
  uint32_t time_high;     /* 0x10: Time high word */
  uint32_t time_low;      /* 0x14: Time low word */
} asknode_who_response_t;

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
