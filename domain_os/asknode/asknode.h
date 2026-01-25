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

/*
 * ============================================================================
 * Status Codes (module 0x11 = NETWORK)
 * ============================================================================
 */
#define status_$network_unknown_network                           0x00110017
#define status_$network_operation_not_defined_on_hardware         0x0011001D
#define status_$network_unknown_request_type                      0x0011000D
#define status_$network_conflict_with_another_node_listing        0x00110019
#define status_$network_quit_fault_during_node_listing            0x0011001A
#define status_$network_waited_too_long_for_more_node_responses   0x0011001B

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
#define ASKNODE_REQ_WHO             0x45    /* WHO query (time sync) */
#define ASKNODE_REQ_WHO_REMOTE      0x2D    /* Remote WHO query */
#define ASKNODE_REQ_LOG_READ        0x31    /* Read log entries */
#define ASKNODE_REQ_DISK_INFO       0x51    /* Get specific disk info */

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
 * ASKNODE_$GET_INFO - Get node information with explicit params
 *
 * Another wrapper for ASKNODE_$INTERNET_INFO with additional control
 * over the parameters passed.
 *
 * @param req_type      Pointer to request type code
 * @param node_id       Pointer to node ID
 * @param param1        First request-specific parameter
 * @param param2        Second request-specific parameter (length/count)
 * @param result        Output buffer for result data
 * @param status        Output status code
 *
 * Original address: 0x00E645C4
 */
void ASKNODE_$GET_INFO(uint16_t *req_type, uint32_t *node_id,
                       uid_t *param1, uint16_t *param2,
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
void ASKNODE_$SERVER(int16_t *response, int32_t *routing_info);

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
