/*
 * NETWORK - Network Operations
 *
 * This module provides network operations for remote objects.
 *
 * The NETWORK subsystem manages network services including:
 * - Page servers for remote paging
 * - Request servers for remote file operations
 * - Service configuration (allowed services bitmap)
 *
 * Key global data area at 0xE248FC contains:
 *   +0x320: Request server count
 *   +0x322: Page server count
 *   +0x342: Allowed service bitmap (32-bit)
 *   +0x344: Remote pool setting
 *   +0x346: Activity flag
 *   +0x348: User socket open flag
 *   +0x34A: Really diskless flag
 *   +0x2A4: Spin lock for network data
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "base/base.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "mmap/mmap.h"

/*
 * Status codes for NETWORK subsystem (module 0x11)
 */
#define status_$network_unknown_request_type            0x0011000D
#define status_$network_request_denied_by_local_node    0x0011000E

/*
 * Network service flags (bits in NETWORK_$ALLOWED_SERVICE)
 *
 * These flags control which network services are enabled:
 *   Bit 0 (0x01): Paging service enabled
 *   Bit 1 (0x02): File service enabled
 *   Bit 2 (0x04): Network service active
 *   Bit 3 (0x08): Routing enabled (auto-set if routing ports exist)
 *   Bit 4 (0x10): Reserved
 *   Bit 18 (0x40000): Extended service info flag
 */
#define NETWORK_SERVICE_PAGING      0x0001
#define NETWORK_SERVICE_FILE        0x0002
#define NETWORK_SERVICE_ACTIVE      0x0004
#define NETWORK_SERVICE_ROUTING     0x0008
#define NETWORK_SERVICE_EXTENDED    0x40000

/*
 * Network set_service operation codes
 */
#define NETWORK_OP_OR_BITS          0   /* OR bits into allowed service */
#define NETWORK_OP_AND_NOT_BITS     1   /* AND NOT bits (clear bits) */
#define NETWORK_OP_SET_VALUE        2   /* Set allowed service directly */
#define NETWORK_OP_SET_REMOTE_POOL  3   /* Set remote pool size */

/*
 * Network global variables
 *
 * These are located in the network data area at 0xE248FC + offset
 */
extern uint32_t NETWORK_$MOTHER_NODE;       /* 0xE24C0C - mother node ID */
extern int16_t NETWORK_$REQUEST_SERVER_CNT;  /* 0xE24C1C (+0x320) */
extern int16_t NETWORK_$PAGE_SERVER_CNT;     /* 0xE24C1E (+0x322) */
extern uint32_t NETWORK_$ALLOWED_SERVICE;    /* 0xE24C3E (+0x342) - 32-bit */
extern int16_t NETWORK_$REMOTE_POOL;         /* 0xE24C40 (+0x344) */
extern int8_t NETWORK_$ACTIVITY_FLAG;        /* 0xE24C46 (+0x346) */
extern char NETWORK_$DO_CHKSUM;
extern int8_t NETWORK_$USER_SOCK_OPEN;       /* 0xE24C48 (+0x34C) */
extern int8_t NETWORK_$REALLY_DISKLESS;      /* 0xE24C4A (+0x34E) */
extern int8_t NETWORK_$DISKLESS;             /* 0xE24C4C (+0x350) - diskless mode */
extern uid_t NETWORK_$PAGING_FILE_UID;

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

/*
 * Note: ROUTE_$N_ROUTING_PORTS is declared in route/route_internal.h
 * Include that header if you need access to it.
 */

/*
 * NETWORK_$READ_AHEAD - Read pages ahead from network partner
 *
 * @param net_info       Network partner info
 * @param uid            UID information
 * @param ppn_array      Physical page number array
 * @param page_size      Page size
 * @param count          Number of pages
 * @param no_read_ahead  Disable read-ahead flag
 * @param flags          Operation flags
 * @param dtm            Data timestamp output
 * @param clock          Clock output
 * @param acl_info       ACL info output
 * @param status         Output status code
 *
 * @return Number of pages successfully read
 */
int16_t NETWORK_$READ_AHEAD(void *net_info, void *uid, uint32_t *ppn_array,
                            uint16_t page_size, int16_t count,
                            int8_t no_read_ahead, uint8_t flags,
                            int32_t *dtm, clock_t *clock,
                            uint32_t *acl_info, status_$t *status);

/*
 * NETWORK_$INSTALL_NET - Install network node
 *
 * Registers a network node in the network table. If the node already exists,
 * increments the reference count. Otherwise, allocates a new slot and stores
 * the network ID.
 *
 * The network index (1-63) is encoded into bits 4-9 of the info parameter.
 *
 * @param node    Network ID to install
 * @param info    Pointer to network info (bits 4-9 receive network index)
 * @param status  Output status code
 *
 * Original address: 0x00E0F1E0
 */
void NETWORK_$INSTALL_NET(uint32_t node, uint16_t *info, status_$t *status);

/*
 * NETWORK_$REMOVE_NET - Remove network node
 *
 * Decrements the reference count for a network. If the reference count
 * reaches zero, the network slot is freed.
 *
 * @param net_addr Network address (bits 4-9 contain network index)
 * @param status   Output status code
 *
 * Original address: 0x00E0F27C
 */
void NETWORK_$REMOVE_NET(uint32_t net_addr, status_$t *status);

/*
 * NETWORK_$GET_NET - Get network ID for a network address
 *
 * Looks up the network ID from the network table using the network index
 * encoded in bits 4-9 of the network address.
 *
 * @param net_addr   Network address (bits 4-9 contain network index)
 * @param net_id_out Output: network ID (0 if index is 0 or not found)
 * @param status     Output status code
 *
 * Original address: 0x00E0F2CC
 */
void NETWORK_$GET_NET(uint32_t net_addr, uint32_t *net_id_out, status_$t *status);

/*
 * NETWORK_$AST_GET_INFO - Get AST info for network object
 *
 * @param uid_info  UID information
 * @param flags     Output flags
 * @param attrs     Output attributes
 * @param status    Output status code
 */
void NETWORK_$AST_GET_INFO(void *uid_info, uint16_t *flags, void *attrs,
                           status_$t *status);

/*
 * NETWORK_$GETHDR - Get a network packet header buffer
 *
 * Allocates a buffer for building network packet headers.
 * If the target is the local node (loopback), allocates from wired memory.
 * Otherwise, uses a shared header page (with lock).
 *
 * @param node_ptr  Pointer to target node ID
 * @param va_out    Output pointer for virtual address
 * @param ppn_out   Output pointer for physical address (ppn << 10)
 *
 * Original address: 0x00E0F37A
 */
void NETWORK_$GETHDR(uint32_t *node_ptr, uint32_t *va_out, uint32_t *ppn_out);

/*
 * NETWORK_$RTNHDR - Return a network packet header buffer
 *
 * Returns a buffer previously obtained from NETWORK_$GETHDR.
 * Frees wired memory or releases the shared header lock.
 *
 * @param va_ptr  Pointer to virtual address to return
 *
 * Original address: 0x00E0F414
 */
void NETWORK_$RTNHDR(uint32_t *va_ptr);

/*
 * NETWORK_$SET_SERVICE - Configure network services
 *
 * Sets or modifies the network service configuration based on the
 * operation code. This controls which services (paging, file, routing)
 * are available on this node.
 *
 * Operations:
 *   0 - OR: Add bits to allowed service
 *   1 - AND NOT: Clear bits from allowed service
 *   2 - SET: Replace allowed service value
 *   3 - SET_REMOTE_POOL: Set the remote pool size
 *
 * If the node is diskless (NETWORK_$DISKLESS < 0), certain services
 * (paging, file) cannot be disabled, and the request will be denied.
 *
 * After setting the service, if routing ports exist and any service
 * is enabled, the routing bit (0x08) is automatically set.
 *
 * @param op_ptr      Pointer to operation code (0-3)
 * @param value_ptr   Pointer to value (service bits or pool size)
 * @param status_p    Output: status code
 *
 * Status codes:
 *   status_$ok: Success
 *   status_$network_unknown_request_type: Invalid operation code
 *   status_$network_request_denied_by_local_node: Cannot disable required service
 *
 * Original address: 0x00E0F45E
 */
void NETWORK_$SET_SERVICE(int16_t *op_ptr, uint32_t *value_ptr, status_$t *status_p);

/*
 * NETWORK_$READ_SERVICE - Read network service configuration
 *
 * Returns the current network service configuration. If the extended
 * service info flag (bit 18) is set in NETWORK_$ALLOWED_SERVICE, returns
 * the full 32-bit allowed service bitmap. Otherwise, returns 0 in the
 * high word and the remote pool size in the low word.
 *
 * @param result_ptr  Output: service configuration (32-bit)
 *                    If extended flag set: full allowed service bitmap
 *                    Otherwise: (0 << 16) | remote_pool_size
 *
 * Original address: 0x00E71D7C
 */
void NETWORK_$READ_SERVICE(uint32_t *result_ptr);

/*
 * NETWORK_$ADD_PAGE_SERVERS - Create page server processes
 *
 * Creates additional network page server processes up to the requested
 * count. Page servers handle incoming page requests from remote nodes
 * performing network paging.
 *
 * On diskless nodes (NETWORK_$REALLY_DISKLESS < 0), at least one page
 * server must remain, so the function stops creating servers if count
 * reaches 1.
 *
 * @param count_ptr    Pointer to desired page server count
 * @param status_ret   Output: status code (set to status_$ok on entry,
 *                     may contain error from PROC1_$CREATE_P)
 *
 * @return Current page server count (may be less than requested on error)
 *
 * Original address: 0x00E71DA4
 */
int16_t NETWORK_$ADD_PAGE_SERVERS(int16_t *count_ptr, status_$t *status_ret);

/*
 * NETWORK_$ADD_REQUEST_SERVERS - Create request server processes
 *
 * Creates additional network request server processes up to the requested
 * count (maximum 3). Request servers handle remote file operations and
 * other network requests.
 *
 * On diskless nodes (NETWORK_$REALLY_DISKLESS < 0), at least one request
 * server must remain, so the function stops creating servers if count
 * reaches 1.
 *
 * @param count_ptr    Pointer to desired request server count (capped at 3)
 * @param status_ret   Output: status code (set to status_$ok on entry,
 *                     may contain error from PROC1_$CREATE_P)
 *
 * @return Current request server count (may be less than requested on error)
 *
 * Original address: 0x00E71E0C
 */
int16_t NETWORK_$ADD_REQUEST_SERVERS(int16_t *count_ptr, status_$t *status_ret);

/*
 * NETWORK_$PAGE_SERVER - Page server main loop
 *
 * Entry point for network page server processes. This function runs
 * as an infinite loop handling page requests.
 *
 * Original address: 0x00E11548
 */
void NETWORK_$PAGE_SERVER(void);

/*
 * NETWORK_$REQUEST_SERVER - Request server main loop
 *
 * Entry point for network request server processes. This function runs
 * as an infinite loop handling remote file and other requests.
 *
 * Original address: 0x00E118DC
 */
void NETWORK_$REQUEST_SERVER(void);

/*
 * ring_info_t - Token Ring network status information
 *
 * Structure returned by NETWORK_$RING_INFO containing status about
 * the token ring network. Total size is 122 bytes (30 longs + 1 word).
 *
 * TODO: Determine the exact layout of this structure from network
 * protocol analysis.
 */
typedef struct ring_info_t {
    uint8_t     data[122];      /* Raw ring info data */
} ring_info_t;

/*
 * NETWORK_$RING_INFO - Get token ring network information
 *
 * Queries the network partner for token ring status information.
 * Sends command 0x0E to the specified network handle and returns
 * 122 bytes of ring information on success.
 *
 * @param net_handle     Network handle/connection to query
 * @param ring_info      Output: ring information buffer (122 bytes)
 * @param status_ret     Output: status code
 *
 * Original address: 0x00E1039A
 */
void NETWORK_$RING_INFO(void *net_handle, ring_info_t *ring_info,
                        status_$t *status_ret);

/*
 * NETWORK_$GET_PKT_SIZE - Get maximum packet size for destination
 *
 * Determines the appropriate packet size to use when communicating with
 * a network destination. For local nodes or local port connections, the
 * caller's requested max_size may be used. For routing through non-local
 * ports, the size is capped at 0x400 (1024 bytes) to ensure compatibility
 * across network segments.
 *
 * @param dest_addr     Pointer to destination address structure:
 *                        +0x00: network port/type (4 bytes)
 *                        +0x04: node ID (4 bytes - low 20 bits used)
 * @param max_size      Caller's requested maximum packet size
 *
 * @return Packet size to use (between 0x400 and max_size)
 *
 * Original address: 0x00E0FA00
 */
uint16_t NETWORK_$GET_PKT_SIZE(uint32_t *dest_addr, uint16_t max_size);

/*
 * NODE_$ME - This node's identifier
 *
 * The low 20 bits of the local node's network address. Used to detect
 * loopback/local destination requests.
 *
 * Original address: 0xE245A4
 */
extern uint32_t NODE_$ME;

#endif /* NETWORK_H */
