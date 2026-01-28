/*
 * REM_FILE - Remote File Operations (Internal)
 *
 * Internal definitions for the remote file operations module.
 * These functions handle file operations on remote (network) nodes.
 */

#ifndef REM_FILE_INTERNAL_H
#define REM_FILE_INTERNAL_H

#include "rem_file/rem_file.h"
#include "base/base.h"
#include "time/time.h"
#include "proc1/proc1.h"
#include "acl/acl.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "network/network.h"

/*
 * Remote file operation codes (sent in request byte 1)
 */
#define REM_FILE_OP_TRUNCATE            0x08
#define REM_FILE_OP_SET_ATTRIBUTE       0x0A
#define REM_FILE_OP_LOCK                0x09
#define REM_FILE_OP_UNLOCK              0x05
#define REM_FILE_OP_NEIGHBORS           0x10
#define REM_FILE_OP_PURIFY              0x0B
#define REM_FILE_OP_SET_DEF_ACL         0x0C
#define REM_FILE_OP_INVALIDATE          0x0E
#define REM_FILE_OP_RESERVE             0x0F
#define REM_FILE_OP_CREATE_TYPE         0x7E  /* Post-init create */
#define REM_FILE_OP_CREATE_TYPE_INIT    0x24  /* Initial create request */
#define REM_FILE_OP_TEST                0x06
#define REM_FILE_OP_NAME_GET_ENTRYU     0x28
#define REM_FILE_OP_NAME_ADD_HARD_LINKU 0x2C
#define REM_FILE_OP_DROP_HARD_LINKU     0x2D
#define REM_FILE_OP_CREATE_AREA         0x02
#define REM_FILE_OP_DELETE_AREA         0x03
#define REM_FILE_OP_GROW_AREA           0x01
#define REM_FILE_OP_ACL_IMAGE           0x1A
#define REM_FILE_OP_ACL_CREATE          0x18
#define REM_FILE_OP_ACL_SETIDS          0x19
#define REM_FILE_OP_ACL_CHECK_RIGHTS    0x17
#define REM_FILE_OP_SET_ACL             0x1B
#define REM_FILE_OP_FILE_SET_PROT       0x22
#define REM_FILE_OP_FILE_SET_ATTRIB     0x23
#define REM_FILE_OP_LOCAL_VERIFY        0x11
#define REM_FILE_OP_LOCAL_READ_LOCK     0x12
#define REM_FILE_OP_GET_SEG_MAP         0x2A
#define REM_FILE_OP_UNLOCK_ALL          0x04
#define REM_FILE_OP_RN_DO_OP            0x80  /* Generic operation marker */

/*
 * Request header (common to all remote file operations)
 *
 * Wire format as sent by REM_FILE_$SEND_REQUEST:
 *   Offset 0-1: uint16_t msg_type  (set to 1 by SEND_REQUEST)
 *   Offset 2:   uint8_t  magic     (0x80, set by caller)
 *   Offset 3:   uint8_t  opcode    (operation code, set by caller)
 *   Offset 4+:  varies             (operation-specific data)
 *
 * Response validation checks response[3] == request[3] + 1.
 */
typedef struct {
    uint16_t msg_type;  /* Set to 1 by SEND_REQUEST before sending */
    uint8_t magic;      /* Always 0x80 */
    uint8_t opcode;     /* Operation code */
    /* Operation-specific data follows */
} rem_file_request_hdr_t;

/*
 * Response buffer size
 * Must be at least 0xE4 (228) bytes to accommodate the largest response structures
 */
#define REM_FILE_RESPONSE_BUF_SIZE  0xE4

/*
 * File status code for communication failures
 */
#define file_$comms_problem_with_remote_node    0x000F0004

/*
 * External data references
 */
extern uint8_t NETWORK_$CAPABLE_FLAGS;  /* 0xE24C3F: Network capability flags (bit 0 = capable) */
extern int8_t NETWORK_$DISKLESS;        /* Diskless node flag */
extern uint32_t NETWORK_$MOTHER_NODE;   /* Mother node ID */

/*
 * Process admin check table
 * Indexed by PROC1_$CURRENT to check if process has admin rights
 */
extern int16_t DAT_00e7daca[];

/*
 * Socket event counter array (0xE28DB0)
 * Indexed by socket number to get the EC pointer for that socket.
 * Despite the name, this is actually an array of EC pointers.
 */
extern ec_$eventcount_t *SOCK_$SOCKET_EC[];

/*
 * PKT info template data at 0xE2E380
 * Used as a packet info parameter for PKT_$SEND_INTERNET.
 */
extern uint8_t DAT_00e2e380[];

/*
 * Per-address-space retry count (accessed via A5-relative addressing)
 * On m68k, A5 points to per-process data:
 *   A5+4: busy retry counter (uint32_t)
 *   A5+8: timeout base (uint16_t)
 */

/*
 * REM_FILE_$SEND_REQUEST - Core network request handler
 *
 * Sends a remote file operation request to a remote node and waits
 * for a response. Handles retransmission, timeouts, and node visibility.
 *
 * @param addr_info     Address info for target node (node at offset +4)
 * @param request       Request buffer (starts with magic + opcode)
 * @param request_len   Length of fixed request portion
 * @param extra_data    Additional request data (can be NULL if extra_len=0)
 * @param extra_len     Length of additional data
 * @param response      Response buffer
 * @param response_max  Maximum response size
 * @param received_len  Output: actual received header length
 * @param bulk_data     Output: bulk data portion (if any)
 * @param bulk_max      Maximum bulk data size
 * @param bulk_len      Output: bulk data length received
 * @param packet_id     Output: packet ID used
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E60FD8
 */
void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data, int16_t bulk_max,
                            int16_t *bulk_len, uint16_t *packet_id,
                            status_$t *status_ret);

/*
 * REM_FILE_$RN_DO_OP - Remote network do operation
 *
 * Higher-level wrapper that prepares security context (SIDs, project lists)
 * before sending a remote file operation.
 *
 * @param addr_info     Address info for target node
 * @param op_buffer     Operation buffer with request data
 * @param fixed_len     Fixed portion length
 * @param op_flags      Operation flags
 * @param response      Response buffer (status at offset +4)
 * @param param_6       Additional parameter
 *
 * Original address: 0x00E61538
 */
void REM_FILE_$RN_DO_OP(void *addr_info, void *op_buffer, int16_t fixed_len,
                        uint16_t op_flags, void *response, void *param_6);

/*
 * Helper macro to check if current process has admin privileges
 */
#define REM_FILE_PROCESS_HAS_ADMIN() \
    (DAT_00e7daca[PROC1_$CURRENT] > 0)

#endif /* REM_FILE_INTERNAL_H */
