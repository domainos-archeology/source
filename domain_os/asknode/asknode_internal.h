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
#include "hint/hint.h"
#include "log/log.h"
#include "misc/misc.h"
#include "mmap/mmap.h"
#include "name/name.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "os/os.h"
#include "pkt/pkt.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "ring/ring.h"
#include "rip/rip.h"
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
 * Network Failure Record
 * ============================================================================
 */

/*
 * Network failure record structure (16 bytes)
 * Located at 0x00E24BF4
 */
typedef struct network_failure_rec_t {
  uint32_t reserved;   /* 0x00: Reserved (first word) */
  uint8_t flags;       /* 0x02 in high: first byte (activity flag) */
  uint8_t pad;         /* 0x03: Padding */
  uint32_t error_info; /* 0x04: Error information */
  uint32_t timestamp;  /* 0x08: Time of failure */
  uint32_t node_id;    /* 0x0C: Node ID involved */
} network_failure_rec_t;

/*
 * ============================================================================
 * External References
 * ============================================================================
 */

/* Network global data */
extern int8_t NETWORK_$ACTIVITY_FLAG; /* 0x00E24C42 */
extern uint8_t
    NETWORK_$FAILURE_REC_2;           /* 0x00E24BF6 - byte within failure rec */
extern uint32_t NETWORK_$FAILURE_REC; /* 0x00E24BF4 - failure record start */
extern int8_t NETWORK_$DISKLESS;      /* Diskless boot flag */
extern uint32_t NETWORK_$MOTHER_NODE; /* Mother node for diskless boot */

/* Network statistics */
extern uint16_t NETWORK_$INFO_RQST_CNT;
extern uint16_t NETWORK_$PAGIN_RQST_CNT;
extern uint16_t NETWORK_$MULT_PAGIN_RQST_CNT;
extern uint16_t NETWORK_$PAGOUT_RQST_CNT;
extern uint16_t NETWORK_$READ_CALL_CNT;
extern uint16_t NETWORK_$WRITE_CALL_CNT;
extern uint16_t NETWORK_$READ_VIOL_CNT;
extern uint16_t NETWORK_$WRITE_VIOL_CNT;
extern uint16_t NETWORK_$BAD_CHKSUM_CNT;

/* Ring network data */
extern uint32_t RING_$DATA;
extern uint32_t RING_$SWDIAG_DATA;
extern uint32_t RING_$SWDIAG_RCVCNT;
extern uint32_t RING_$SWDIAG_NODEID;
extern uint16_t RING_$XMIT_BIPHASE;
extern uint16_t RING_$RCV_BIPHASE;
extern uint16_t RING_$XMIT_ESB;
extern uint16_t RING_$RCV_ESB;

/* Memory stats */
extern uint32_t MEM_$MEM_REC;
extern uint32_t MMAP_$REAL_PAGES;

/* PROM data */
extern uint32_t PROM_$SAU_AND_AUX;

/* MMU data */
extern uint32_t MMU_$SYSTEM_REV;

/* GPU data */
extern int8_t GPU_$PRESENT;

/* Packet info data block at 0x00E82408 */
extern uint32_t DAT_00e82408;

/* Socket event counts */
extern ec_$eventcount_t *DAT_00e28db0; /* Socket EC array base - 4 */
extern ec_$eventcount_t *DAT_00e28dc4; /* Socket 5 EC */

/* Network capability flags at 0x00E24C3F */
extern uint8_t DAT_00e24c3f;

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * FUN_00e65904 - Check network capability for node
 *
 * Checks if a network operation is supported for the given routing info.
 *
 * @param routing_key   Routing information
 * @param is_local      Non-zero if querying local node
 *
 * Returns:
 *   0: Unknown network
 *   1: Network supports operation
 *   2: Operation not defined on hardware
 *
 * Original address: 0x00E65904
 */
int16_t FUN_00e65904(int32_t routing_key, int8_t is_local);

#endif /* ASKNODE_INTERNAL_H */
