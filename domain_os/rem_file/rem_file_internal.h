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
#include "uid/uid.h"
#include "ast/ast.h"
#include "ml/ml.h"
#include "netlog/netlog.h"

/*
 * =============================================================================
 * Remote file operation codes - request byte +0x03
 * =============================================================================
 *
 * Every REM_FILE_$* request builder writes the same two bytes at the head of
 * its request record: 0x80 at +0x02 and the operation code at +0x03
 * ("move.b #-0x80,(-0x16e,A6)" / "move.b #op,(-0x16d,A6)", the record being
 * based at A6-0x170; REM_FILE_$CREATE_TYPE and REM_FILE_$NAME_GET_ENTRYU use
 * A6-0x198 and REM_FILE_$UNLOCK_ALL A6-0xD0, same two offsets into the
 * record).  REM_FILE_$SERVER reads it back at request+3 - "move.b
 * (-0x435,A6),D0b" with the request at A6-0x438 (0x00E63A02 and 17 other
 * sites).
 *
 * The whole table below was re-derived from the image for bead source-8joj;
 * the values it replaced were guesses and almost all of them were wrong.
 * Every code is even.  Before reaching its own dispatch, REM_FILE_$SERVER
 * forwards two ranges wholesale:
 *
 *   0x2A..0x5C  -> DIR_$SERVER (0x00E58200)   - 0x00E63840-0x00E6384C
 *   0x64..0x77  -> ACL_$SERVER (0x00E49594)   - 0x00E6395E-0x00E6396A
 *
 * so the ACL codes below are handled by ACL_$SERVER, not by REM_FILE_$SERVER.
 * Everything else falls through to the compare chain at 0x00E63A02-0x00E63AD0;
 * an unrecognised code lands at 0x00E64136.
 */
#define REM_FILE_OP_TEST                0x00  /* 0x00E62386 clr.b; server 0x00E63E40 */
#define REM_FILE_OP_SET_ATTRIBUTE       0x04  /* 0x00E61A48; server __set_attribute   */
#define REM_FILE_OP_TRUNCATE            0x08  /* 0x00E61996; server __truncate_delete */
#define REM_FILE_OP_LOCK                0x0A  /* 0x00E61B5E; the plain lock           */
#define REM_FILE_OP_UNLOCK              0x0C  /* 0x00E61D48; server 0x00E63C8C        */
#define REM_FILE_OP_NEIGHBORS           0x10  /* 0x00E621D0; server 0x00E63ADC        */
#define REM_FILE_OP_UNLOCK_ALL          0x12  /* 0x00E61C84; server 0x00E63D18        */
#define REM_FILE_OP_PURIFY              0x14  /* 0x00E62272; server 0x00E63E16        */
#define REM_FILE_OP_LOCAL_READ_LOCK     0x16  /* 0x00E61EB6; server 0x00E63DDA        */
#define REM_FILE_OP_SET_DEF_ACL         0x18  /* 0x00E62300; server 0x00E63E48        */
#define REM_FILE_OP_LOCAL_VERIFY        0x1A  /* 0x00E61E38; server 0x00E63E02        */
#define REM_FILE_OP_NAME_GET_ENTRYU     0x1C  /* 0x00E620C2; server 0x00E63DFA        */
#define REM_FILE_OP_GET_SEG_MAP         0x1E  /* 0x00E61F62; server 0x00E63EA4        */
#define REM_FILE_OP_INVALIDATE          0x20  /* 0x00E623F8; server 0x00E63F30        */
#define REM_FILE_OP_NAME_ADD_HARD_LINKU 0x22  /* 0x00E6250A; server 0x00E63F6E        */
#define REM_FILE_OP_GENERATE_UID        0x24  /* server __generate_uid (0x00E632C2).
                                               * Phase 1 of REM_FILE_$CREATE_TYPE
                                               * (0x00E61742), CREATE_TYPE_PRESR10
                                               * (0x00E6188C) and ACL_CREATE
                                               * (0x00E62858) */
#define REM_FILE_OP_CREATE_TYPE_PRESR10 0x26  /* 0x00E618E4; server 0x00E63FD6        */
#define REM_FILE_OP_DROP_HARD_LINKU     0x28  /* 0x00E625AC; server 0x00E63FC6        */

/* 0x64..0x77 - forwarded to ACL_$SERVER */
#define REM_FILE_OP_ACL_IMAGE           0x64  /* 0x00E627C0 */
#define REM_FILE_OP_SET_ACL             0x66  /* 0x00E62AC4 */
#define REM_FILE_OP_ACL_CREATE          0x68  /* 0x00E628A2, phase 2 of ACL_CREATE */
#define REM_FILE_OP_ACL_SETIDS          0x6A  /* 0x00E62950 */
#define REM_FILE_OP_ACL_CHECK_RIGHTS    0x6C  /* 0x00E62A10 */

#define REM_FILE_OP_RESERVE             0x7C  /* 0x00E62478; server 0x00E63F50        */
#define REM_FILE_OP_CREATE_TYPE         0x7E  /* 0x00E61788; server 0x00E64020,
                                               * phase 2 of REM_FILE_$CREATE_TYPE */
#define REM_FILE_OP_FILE_SET_PROT       0x80  /* 0x00E62B84; server __set_prot_attrib */
#define REM_FILE_OP_FILE_SET_ATTRIB     0x82  /* 0x00E62C46; same server handler      */
#define REM_FILE_OP_LOCK_EXT            0x84  /* 0x00E61AEA; the extended lock, which
                                               * shares the 0x0A handler
                                               * (0x00E63B08) but carries the full
                                               * lock record and an ACL check */
#define REM_FILE_OP_CREATE_AREA         0x86  /* 0x00E62646; server 0x00E640A8        */
#define REM_FILE_OP_DELETE_AREA         0x88  /* 0x00E626E6; server 0x00E640EC        */
#define REM_FILE_OP_GROW_AREA           0x8A  /* 0x00E62754; server 0x00E64106        */

/* The constant byte every builder writes at request+0x02. */
#define REM_FILE_REQ_MAGIC              0x80

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
 * (NETWORK_$DISKLESS, NETWORK_$REALLY_DISKLESS, NETWORK_$MOTHER_NODE and
 * NODE_$ME come from network/network.h; NETLOG_$OK_TO_LOG_SERVER from
 * netlog/netlog.h; UID_$NIL from uid/uid.h; ACL_$SUPER_COUNT from acl/acl.h.)
 */
extern uint8_t NETWORK_$CAPABLE_FLAGS;  /* 0xE24C3F: Network capability flags (bit 0 = capable) */

/*
 * The request is a wire record whose payload is interpreted differently by
 * every opcode, so it is kept as an opaque byte area and reached through the
 * REQ_* accessors below.  The three fields the dispatcher itself reads are
 * named.
 */
typedef struct rem_file_server_req_t {
    uint16_t    version;            /* 0x000: A6-0x438 */
    uint8_t     reserved_02;        /* 0x002: A6-0x436 */
    uint8_t     opcode;             /* 0x003: A6-0x435 */
    uid_t       uid;                /* 0x004: A6-0x434 */
    uint8_t     arg[0x28C];         /* 0x00C: A6-0x42C .. */
} rem_file_server_req_t;            /* 0x298 */

typedef struct rem_file_server_resp_t {
    uint16_t    pkt_flag;           /* 0x000: A6-0x1A0 */
    uint8_t     magic;              /* 0x002: A6-0x19E */
    uint8_t     opcode;             /* 0x003: A6-0x19D */
    status_$t   status;             /* 0x004: A6-0x19C */
    uint8_t     data[0x118];        /* 0x008: A6-0x198 .. */
} rem_file_server_resp_t;           /* 0x120 */

/*
 * The block APP_$RECEIVE fills in (A6-0x30, 0x30 bytes).  The first two
 * longwords are real pointers, so they are typed as such here; on a 64-bit
 * host that makes the record wider than the image's 0x30 bytes, which only
 * the m68k `_Static_assert`s care about.
 */
typedef struct rem_file_rcv_t {
    void       *hdr;                /* 0x00: -0x30, packet header record */
    void       *data;               /* 0x04: -0x2C, request body */
    uint32_t    bufs[4];            /* 0x08: -0x28, network buffer chain */
    uint32_t    f_18;               /* 0x18: -0x18 */
    uint32_t    f_1c;               /* 0x1C: -0x14 */
    uint8_t     clock[6];           /* 0x20: -0x10, 48-bit arrival clock */
    uint32_t    f_26;               /* 0x26: -0x0A */
    uint16_t    pad_2a[3];          /* 0x2A */
} rem_file_rcv_t;

/*
 * REM_FILE module data
 */
extern ml_$exclusion_t REM_FILE_$SOCK_LOCK;   /* 0xE24B3C: socket access lock */

/*
 * 0xE823FC is the REM_FILE module base (A5 in REM_FILE_$SERVER).  The
 * longword at A5+0 counts the stale-directory-entry replies the server has
 * sent (`addq.l #1,(A5)` at 0x00E63E9E).
 */
extern uint32_t REM_FILE_$STALE_LINK_COUNT;

/* 0xE64592: the status constant CRASH_SYSTEM is handed when the diskless
 * partner node dies (0x000F0004). */
extern status_$t REM_FILE_$COMMS_PROBLEM_STATUS;

/* 0xE61D18: longword 0.  Passed by reference wherever the compiler emits
 * `pea (d,PC)` for a NIL pointer / empty segment list. */
extern uint32_t REM_FILE_$NIL_CONST;

/* 0xE61718: word 8, the maximum project-list length ACL_$GET_PROJ_LIST and
 * ACL_$SET_PROJ_LIST are given. */
extern uint16_t REM_FILE_$MAX_PROJ_LIST;

/* 0xE62D48: word 0x20, the output buffer size UNMAP_CASE / MAP_CASE get. */
extern uint16_t REM_FILE_$MAX_NAME_LEN;

/* 0xE2E39E: the 32-byte packet template REM_FILE_$SERVER hands
 * PKT_$SEND_INTERNET (0x00E64236). */
extern uint8_t REM_FILE_$SERVER_PKT_INFO[];

/* 0xE2E3BC: "*** diskless partner node has crashed" */
extern char REM_FILE_$DISKLESS_CRASH_MSG[];

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
 * (tst.w (-0x2,A0,D1w*2) with A0 = 0xe7dacc, i.e. ACL_$SUPER_COUNT[PROC1_$CURRENT])
 */
#define REM_FILE_PROCESS_HAS_ADMIN() \
    (ACL_$SUPER_COUNT[PROC1_$CURRENT] > 0)

#endif /* REM_FILE_INTERNAL_H */
