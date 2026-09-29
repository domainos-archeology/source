/*
 * ASKNODE - Node Query Subsystem
 *
 * This module provides functions for querying information about network nodes.
 * It supports:
 * - Getting node statistics and configuration
 * - WHO queries for node discovery
 * - Network failure record management
 * - Server-side request handling
 *
 * The ASKNODE subsystem is part of Domain/OS's distributed computing model,
 * allowing nodes to query each other for information about system state,
 * disk usage, process lists, and more.
 */

#ifndef ASKNODE_H
#define ASKNODE_H

#include "base/base.h"
#include "network/network.h"

/*
 * Status codes (module 0x11 = NETWORK) are defined in network/network.h.
 */

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */
#define ASKNODE_MAX_WHO_COUNT       2000    /* Maximum nodes to return in WHO */
#define ASKNODE_DONE_MARKER         0xDEAF  /* -0x2151 = Propagation complete marker */

/*
 * Request type codes for ASKNODE_$INTERNET_INFO
 */
#define ASKNODE_REQ_BOOT_TIME       0x02    /* Get boot time */
#define ASKNODE_REQ_NODE_UID        0x04    /* Get node UID */
#define ASKNODE_REQ_STATS           0x06    /* Get node statistics */
#define ASKNODE_REQ_TIMEZONE        0x08    /* Get timezone info */
#define ASKNODE_REQ_VOLUME_INFO     0x0A    /* Get volume info */
#define ASKNODE_REQ_PAGING_INFO     0x0C    /* Get paging file info */
#define ASKNODE_REQ_RECORD_FAILURE  0x0E    /* Record failure */
#define ASKNODE_REQ_DISK_STATS      0x10    /* Get disk stats (all disks) */
#define ASKNODE_REQ_PROC_LIST       0x12    /* Get process list */
#define ASKNODE_REQ_PROC_INFO       0x14    /* Get process info */
#define ASKNODE_REQ_SIGNAL          0x16    /* Signal process group */
#define ASKNODE_REQ_ROOT_UID        0x18    /* Get root UID */
#define ASKNODE_REQ_BUILD_TIME      0x1A    /* Get build time */
#define ASKNODE_REQ_UIDS            0x1C    /* Get multiple UIDs */
#define ASKNODE_REQ_NETWORK_DIAG    0x1F    /* Network diagnostics */
#define ASKNODE_REQ_PROC_INFO2      0x21    /* Get extended process info */
#define ASKNODE_REQ_PROC_UPIDS      0x23    /* Get process UPIDs */
#define ASKNODE_REQ_LOG_CONTROL     0x25    /* Control ring/net logging */
#define ASKNODE_REQ_SYSTEM_INFO     0x27    /* Get system configuration */
#define ASKNODE_REQ_WHO             0x00    /* WHO enumeration - ASKNODE_$SERVER
                                             * jump-table entry 0 (0x00E65B18) */
#define ASKNODE_REQ_TIME_SYNC       0x45    /* WHO query with time sync -
                                             * entry 0x45 (0x00E65C7E) */
#define ASKNODE_REQ_WHO_REMOTE      0x2D    /* Remote WHO query (0x00E65BA8) */
#define ASKNODE_REQ_LOG_READ        0x31    /* Read log entries (0x00E65D24) */
#define ASKNODE_REQ_NET_STATS       0x29    /* Network/ring counters (0x00E64DE2) */
#define ASKNODE_REQ_PROC_PID        0x2B    /* UID -> PID (0x00E64F06) */
#define ASKNODE_REQ_FAILURE_REC     0x2F    /* NETWORK_$FAILURE_REC (0x00E64F1E) */
#define ASKNODE_REQ_PROC1_LIST      0x33    /* PROC1 bound-process list (0x00E64F78) */
#define ASKNODE_REQ_SIGNAL2         0x35    /* Signal, selector in param+0x0C
                                             * (0x00E64F8E) */
#define ASKNODE_REQ_ROUTE_PORT      0x37    /* ROUTE_$PORT (0x00E64FC6) */
#define ASKNODE_REQ_PORT_LIST       0x39    /* All open routing ports (0x00E64FD2) */
#define ASKNODE_REQ_PORT_INFO       0x3B    /* One port, short form (0x00E65088) */
#define ASKNODE_REQ_DEVICE_STAT     0x3D    /* NET_IO_$DEVICE_STAT (0x00E6514A) */
#define ASKNODE_REQ_ROUTE_STATS     0x3F    /* Routing counters (0x00E650C6) */
#define ASKNODE_REQ_NET_ROUTE       0x41    /* Route to one network (0x00E6524C) */
#define ASKNODE_REQ_QUEUE_DEPTH     0x43    /* ROUTE_$Q_DEPTH histogram (0x00E65328) */
#define ASKNODE_REQ_BOOT_DEVICE     0x47    /* OS_$BOOT_DEVICE (0x00E65356) */
#define ASKNODE_REQ_LOADAV          0x49    /* PROC1_$GET_LOADAV (0x00E6536C) */
#define ASKNODE_REQ_PROC_WS_INFO    0x4B    /* Working-set sizes (0x00E6538C) */
#define ASKNODE_REQ_REV_INFO        0x4D    /* OS_$GET_REV_INFO (0x00E6543E) */
#define ASKNODE_REQ_ZOMBIE_LIST     0x4F    /* PROC2_$ZOMBIE_LIST (0x00E649C2) */
#define ASKNODE_REQ_DISK_INFO       0x51    /* Get specific disk info */
#define ASKNODE_REQ_DISPLAY_LIST    0x55    /* SMD display table (0x00E6544C) */
#define ASKNODE_REQ_PROC_LIST2      0x57    /* PROC2_$LIST2 (0x00E649EC) */
#define ASKNODE_REQ_ZOMBIE_LIST2    0x59    /* PROC2_$ZOMBIE_LIST, reply form 2
                                             * (0x00E64A0C) */
#define ASKNODE_REQ_DEVICE_STAT2    0x5B    /* NET_IO_$DEVICE_STAT2 (0x00E6514A) */

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * ASKNODE_$INFO - Get node information (local node only)
 *
 * Simplified wrapper for ASKNODE_$INTERNET_INFO that queries the local node.
 * The request type is specified in *req_type.
 *
 * The routing/request-length and reply-length arguments come from the two-cell
 * constant pool at 0x00E645BE..0x00E645C3 (0x0098 and 0xFFFFFFFF); see
 * asknode/info.c.
 *
 * @param req_type      Pointer to request type code
 * @param node_id       Pointer to node ID (ignored, always uses local)
 * @param param         Request-specific parameter
 * @param result        Output buffer for result data
 * @param status        Output status code
 *
 * Original address: 0x00E64598
 */
void ASKNODE_$INFO(uint16_t *req_type, uint32_t *node_id,
                   uid_t *param, uint32_t *result, status_$t *status);

/*
 * ASKNODE_$GET_INFO - Get node information, caller-supplied reply limit
 *
 * The same wrapper as ASKNODE_$INFO except that the caller supplies
 * ASKNODE_$INTERNET_INFO's fifth argument (the reply-length limit) instead of
 * taking it from the constant pool; the third argument still comes from
 * 0x00E645C0 ("pea (-0x1a,PC)" at 0x00E645D8).  See asknode/get_info.c.
 *
 * @param req_type      Pointer to request type code
 * @param node_id       Pointer to node ID
 * @param param         Request-specific parameter (often a UID)
 * @param resp_len      Pointer to the reply-length limit
 * @param result        Output buffer for result data
 * @param status        Output status code
 *
 * Original address: 0x00E645C4
 */
void ASKNODE_$GET_INFO(uint16_t *req_type, uint32_t *node_id,
                       uid_t *param, uint16_t *resp_len,
                       uint32_t *result, status_$t *status);

/*
 * ASKNODE_$INTERNET_INFO - Get detailed node information
 *
 * Main function for querying node information over the network.
 * Supports many request types for different kinds of data.
 * For local node queries, retrieves data directly.
 * For remote nodes, sends a network request and waits for response.
 *
 * @param req_type      Pointer to request type code
 * @param node_id       Pointer to target node ID (NODE_$ME or 0 for local)
 * @param req_len       Pointer to request length
 * @param param         Request-specific parameter (often a UID)
 * @param resp_len      Pointer to response length limit
 * @param result        Output buffer for result data
 * @param status        Output status code
 *
 * Original address: 0x00E645EA
 */
uint32_t ASKNODE_$INTERNET_INFO(uint16_t *req_type, uint32_t *node_id,
                                int32_t *req_len, uid_t *param,
                                uint16_t *resp_len, uint32_t *result,
                                status_$t *status);

/*
 * ASKNODE_$READ_FAILURE_REC - Read network failure record
 *
 * Reads the current network failure record, which tracks the last
 * network failure that occurred. The record is 16 bytes.
 *
 * @param record        Output buffer for 16-byte failure record
 *
 * Original address: 0x00E658D0
 */
void ASKNODE_$READ_FAILURE_REC(uint32_t *record);

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


_Static_assert(offsetof(asknode_$server_ctx_t, param3)     == 0x14, "server_ctx.param3");
_Static_assert(offsetof(asknode_$server_ctx_t, request_id) == 0x18, "server_ctx.request_id");
_Static_assert(offsetof(asknode_$server_ctx_t, socket)     == 0x1A, "server_ctx.socket");
_Static_assert(offsetof(asknode_$server_ctx_t, clock_hi)   == 0x1C, "server_ctx.clock_hi");
_Static_assert(offsetof(asknode_$server_ctx_t, clock_lo)   == 0x1E, "server_ctx.clock_lo");
_Static_assert(sizeof(asknode_$server_ctx_t) == 0x22, "server_ctx: 0x22 bytes");

/*
 * ASKNODE_$SERVER - Handle incoming node query requests
 *
 * Server function that processes incoming ASKNODE requests from other nodes.
 * Receives a request packet, processes it, and sends a response.
 *
 * @param response      Response buffer (34 bytes)
 * @param routing_info  Routing information for reply
 *
 * Original address: 0x00E6597A
 */
void ASKNODE_$SERVER(struct asknode_$server_ctx_t *ctx, int32_t *routing_info);

/*
 * ASKNODE_$PROPAGATE_WHO - Propagate WHO response to network
 *
 * Sends a WHO response packet to propagate node information
 * across the network. Used for network topology discovery.
 *
 * @param response      Response data (from previous WHO query)
 * @param routing_info  Routing information
 *
 * Original address: 0x00E65E8E
 */
void ASKNODE_$PROPAGATE_WHO(int16_t *response, uint32_t *routing_info);

/*
 * ASKNODE_$WHO - List nodes on network
 *
 * Primary function for listing all nodes on the network.
 * Tries WHO_REMOTE first (using topology information), and falls
 * back to WHO_NOTOPO if that fails.
 *
 * @param node_list     Output array of node IDs
 * @param max_count     Maximum number of nodes to return
 * @param count         Output: actual number of nodes found
 *
 * Original address: 0x00E65F78
 */
void ASKNODE_$WHO(int32_t *node_list, int16_t *max_count, uint16_t *count);

/*
 * ASKNODE_$WHO_NOTOPO - List nodes without topology support
 *
 * Lists network nodes using broadcast queries rather than
 * topology-based routing. Used as fallback when topology
 * information is not available.
 *
 * @param node_id       Pointer to local node ID
 * @param port          Pointer to port number
 * @param node_list     Output array of node IDs
 * @param max_count     Maximum number of nodes to return
 * @param count         Output: actual number of nodes found
 * @param status        Output status code
 *
 * Original address: 0x00E65FDC
 */
void ASKNODE_$WHO_NOTOPO(int32_t *node_id, int32_t *port,
                         int32_t *node_list, int16_t *max_count,
                         uint16_t *count, status_$t *status);

/*
 * ASKNODE_$WHO_REMOTE - List nodes using remote topology
 *
 * Lists network nodes using the network topology for efficient
 * routing of WHO queries. Supports multi-hop networks.
 *
 * @param node_id       Pointer to local node ID
 * @param port          Pointer to port number
 * @param node_list     Output array of node IDs
 * @param max_count     Maximum number of nodes to return
 * @param count         Output: actual number of nodes found
 * @param status        Output status code
 *
 * Original address: 0x00E66334
 */
void ASKNODE_$WHO_REMOTE(int32_t *node_id, int32_t *port,
                         int32_t *node_list, int16_t *max_count,
                         uint16_t *count, status_$t *status);

#endif /* ASKNODE_H */
