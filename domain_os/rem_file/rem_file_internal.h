/*
 * REM_FILE - Remote File Operations (Internal)
 *
 * Internal definitions for the remote file operations module.
 * These functions handle file operations on remote (network) nodes.
 */

#ifndef REM_FILE_INTERNAL_H
#define REM_FILE_INTERNAL_H

#include "app/app.h"       /* app_$reply_hdr_t, app_$receive_rec_t */
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
#include "network/network.h"   /* NETWORK_$CAPABLE_FLAGS */

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
 * Fixed request-record lengths - the literal word each builder pushes as
 * REM_FILE_$SEND_REQUEST's third argument.  Each is the size of the record
 * based at that builder's A6-0x170, including trailing bytes the builder
 * never stores (the frame is not cleared, so those go out as-is).
 */
#define REM_FILE_SET_ATTRIBUTE_REQ_LEN  0x42  /* 0x00E61A96 */
#define REM_FILE_LOCAL_VERIFY_REQ_LEN   0x2E  /* 0x00E61E80 */
#define REM_FILE_PURIFY_REQ_LEN         0x14  /* 0x00E622D6 */
#define REM_FILE_SET_DEF_ACL_REQ_LEN    0x20  /* 0x00E6235C */
#define REM_FILE_INVALIDATE_REQ_LEN     0x16  /* 0x00E6243E */
#define REM_FILE_DELETE_AREA_REQ_LEN    0x1C  /* 0x00E6271C */
#define REM_FILE_GROW_AREA_REQ_LEN      0x1C  /* 0x00E6278E */
#define REM_FILE_ACL_IMAGE_REQ_LEN      0x14  /* 0x00E62806 */

/*
 * The bulk-payload ceiling REM_FILE_$ACL_IMAGE offers the transport
 * ("move.w #0x400,-(SP)" at 0x00E627EC).
 */
#define REM_FILE_ACL_IMAGE_BULK_MAX     0x400

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
 * REM_FILE_RESPONSE_BUF_SIZE - the reply buffer size, 0xBE bytes.
 *
 * Every rem_file client declares the buffer at A6-0xC0 and hands
 * REM_FILE_$SEND_REQUEST the literal 0xBE as `response_max`
 * ("move.w #0xbe,-(SP)" - 26 call sites, e.g. 0x00E619E8 in TRUNCATE,
 * 0x00E62224 in NEIGHBORS, 0x00E62B3C in SET_ACL).  REM_FILE_$CREATE_TYPE is
 * the only one whose buffer sits elsewhere in the frame (A6-0xE8); its length
 * is still 0xBE.
 *
 * A reply field's record offset is therefore its A6 displacement plus 0xC0:
 * "move.l (-0xb8,A6)" is response+0x08.  The tree used to assume the buffer
 * started at A6-0xE4, which put every recovered field 0x24 bytes too far
 * along (bead source-r2te).
 */
#define REM_FILE_RESPONSE_BUF_SIZE  0xBE

/*
 * rem_file_$response_t - the fixed head every reply carries.  It is the same
 * shape REM_FILE_$SERVER builds on the far side (rem_file_server_resp_t):
 * the opcode at +0x03 is the request opcode plus one, and the server's own
 * status is the longword at +0x04 that REM_FILE_$SEND_REQUEST copies into the
 * caller's status (0x00E6146C).  Payloads start at +0x08.
 */
typedef struct rem_file_$response_t {
    uint16_t    pkt_flag;       /* 0x00 */
    uint8_t     magic;          /* 0x02 */
    uint8_t     opcode;         /* 0x03 */
    status_$t   status;         /* 0x04 */
    uint8_t     data[REM_FILE_RESPONSE_BUF_SIZE - 8];  /* 0x08 */
} rem_file_$response_t;

_Static_assert(__builtin_offsetof(rem_file_$response_t, opcode) == 0x03, "rem_file_$response_t.opcode");
_Static_assert(__builtin_offsetof(rem_file_$response_t, status) == 0x04, "rem_file_$response_t.status");
_Static_assert(__builtin_offsetof(rem_file_$response_t, data) == 0x08, "rem_file_$response_t.data");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(rem_file_server_req_t, version) == 0x00, "rem_file_server_req_t.version");
_Static_assert(__builtin_offsetof(rem_file_server_req_t, reserved_02) == 0x02, "rem_file_server_req_t.reserved_02");
_Static_assert(__builtin_offsetof(rem_file_server_req_t, opcode) == 0x03, "rem_file_server_req_t.opcode");
_Static_assert(__builtin_offsetof(rem_file_server_req_t, uid) == 0x04, "rem_file_server_req_t.uid");
_Static_assert(__builtin_offsetof(rem_file_server_req_t, arg) == 0x0C, "rem_file_server_req_t.arg");

typedef struct rem_file_server_resp_t {
    uint16_t    pkt_flag;           /* 0x000: A6-0x1A0 */
    uint8_t     magic;              /* 0x002: A6-0x19E */
    uint8_t     opcode;             /* 0x003: A6-0x19D */
    status_$t   status;             /* 0x004: A6-0x19C */
    uint8_t     data[0x118];        /* 0x008: A6-0x198 .. */
} rem_file_server_resp_t;           /* 0x120 */

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(rem_file_server_resp_t, pkt_flag) == 0x00, "rem_file_server_resp_t.pkt_flag");
_Static_assert(__builtin_offsetof(rem_file_server_resp_t, magic) == 0x02, "rem_file_server_resp_t.magic");
_Static_assert(__builtin_offsetof(rem_file_server_resp_t, opcode) == 0x03, "rem_file_server_resp_t.opcode");
_Static_assert(__builtin_offsetof(rem_file_server_resp_t, status) == 0x04, "rem_file_server_resp_t.status");
_Static_assert(__builtin_offsetof(rem_file_server_resp_t, data) == 0x08, "rem_file_server_resp_t.data");

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

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(rem_file_rcv_t, hdr) == 0x00, "rem_file_rcv_t.hdr");
_Static_assert(__builtin_offsetof(rem_file_rcv_t, data) == 0x04, "rem_file_rcv_t.data");
_Static_assert(__builtin_offsetof(rem_file_rcv_t, bufs) == 0x08, "rem_file_rcv_t.bufs");
_Static_assert(__builtin_offsetof(rem_file_rcv_t, f_18) == 0x18, "rem_file_rcv_t.f_18");
_Static_assert(__builtin_offsetof(rem_file_rcv_t, f_1c) == 0x1C, "rem_file_rcv_t.f_1c");
_Static_assert(__builtin_offsetof(rem_file_rcv_t, clock) == 0x20, "rem_file_rcv_t.clock");
_Static_assert(__builtin_offsetof(rem_file_rcv_t, f_26) == 0x26, "rem_file_rcv_t.f_26");
_Static_assert(__builtin_offsetof(rem_file_rcv_t, pad_2a) == 0x2A, "rem_file_rcv_t.pad_2a");
_Static_assert(sizeof(rem_file_rcv_t) == 0x30, "rem_file_rcv_t size");
#endif

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

/*
 * 0xE82400 (A5+0x04): longword, bumped once per "server busy" reply that
 * REM_FILE_$SEND_REQUEST has to retry (`addq.l #1,(0x4,A5)` at 0x00E6141C).
 */
extern uint32_t REM_FILE_$BUSY_RETRY_COUNT;

/*
 * 0xE82404 (A5+0x08): word, image value 0x0014 (20 ticks).  The base
 * completion allowance REM_FILE_$SEND_REQUEST adds to TIME_$CLOCKH and to
 * the per-request send overhead to get the response deadline
 * (`move.w (0x8,A5),D1w` at 0x00E611CE, zero-extended).
 */
extern uint16_t REM_FILE_$COMPLETION_TIME;

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

/* The socket pointer table is owned by sock/sock.h (SOCK_$DATA.socket_ptr,
 * map SOCK_$SOCKET_PTR); the unused SOCK_$SOCKET_EC alias that used to be
 * declared here was removed (bead source-3uo). */

/*
 * REM_FILE_$DATA - the client-side packet-info template at 0x00E2E380,
 * 0x1E bytes, handed to PKT_$SEND_INTERNET by REM_FILE_$SEND_REQUEST
 * (`move.l #0xe2e380,-(SP)` at 0x00E61178) and copied into a local by
 * REM_FILE_$UNLOCK_ALL (0x00E61C9C).
 *
 * The SAU2 10.2 map names the whole wired segment after it:
 *   D33 E2E380  REM_FILE_$DATA   loaded at 12FB80, size = 7C
 * so REM_FILE_$SERVER_PKT_INFO (0x00E2E39E) and
 * REM_FILE_$DISKLESS_CRASH_MSG (0x00E2E3BC) are interior objects of the same
 * segment; only its first object carries the exported name.
 */
extern uint8_t REM_FILE_$DATA[];

/*
 * The reply header APP_$RECEIVE hands back at app_$receive_rec_t.reply
 * (0x00E6125A `movea.l (-0x30,A6),A0`) is the shared eight-byte
 * app_$reply_hdr_t (app/app.h).  REM_FILE reads three of its four words:
 *   prefix.template_len  0x00E6126E, the reply header length
 *   prefix.data_len      0x00E61262, the bulk payload length
 *   prefix.request_id    0x00E61266, matched against pkt_id
 * and never looks at prefix.magic.  (source-ca0z)
 */

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
 * rem_file_$rn_op_buf_t - the request buffer REM_FILE_$RN_DO_OP is handed
 * (0x00E61538, the record reached through A1 = (0xC,A6)).
 *
 * The head is the ordinary REM_FILE request header; 0x14..0x8D is the
 * security context ACL_$GET_RE_ALL_SIDS and ACL_$GET_PROJ_LIST fill in place
 * (0x00E6154E-0x00E61590), and everything from 0x8E on is interpreted
 * differently for each of the four DIR opcodes RN_DO_OP knows about, so it is
 * modelled as a union of per-opcode views.  Every offset below is the literal
 * A1 displacement in the listing.
 *
 * The 0x122 and 0x108 request-length caps at 0x00E615E0 / 0x00E61626 are the
 * ends of the two inline areas: 0xB0 + 0x72 = 0x122 for the 0x58 view and
 * 0x96 + dest_off + len <= 0x108 for the 0x3C view.
 */
typedef struct rem_file_$rn_op_buf_t {
    uint16_t    msg_type;           /* 0x00: stamped by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;              /* 0x02: 0x80 (0x00E615BE) */
    uint8_t     op_code;            /* 0x03: a DIR_$SERVER opcode */
    uint8_t     op_data[0x10];      /* 0x04: opcode-specific */
    uint8_t     re_sids[0x14];      /* 0x14: ACL_$GET_RE_ALL_SIDS arg 4.
                                     *       Byte 0x0D (record offset 0x21)
                                     *       gets bit 2 set when the caller is
                                     *       in a subsystem (0x00E615B4). */
    uint8_t     sids[0x24];         /* 0x28: ACL_$GET_RE_ALL_SIDS arg 2 */
    uint8_t     proj_list[0x40];    /* 0x4C: ACL_$GET_PROJ_LIST arg 1 */
    int16_t     proj_count;         /* 0x8C: ACL_$GET_PROJ_LIST arg 3 */
    /* The tail begins at 0x8E, which is only 2-aligned, so the union is
     * packed (the m68k ABI aligns longwords to 2 anyway; a 64-bit host does
     * not). */
    union __attribute__((packed, aligned(2))) {
        /* 0x58 SERVER_OP_DIR_LIST (0x00E615D2, 0x00E6167E) */
        struct {
            uint32_t data_va;       /* 0x8E: source of the outbound data */
            uint16_t data_len;      /* 0x92: its byte count */
            uint8_t  _pad_94[0x18]; /* 0x94 */
            uint32_t reply_va;      /* 0xAC: where the bulk reply goes */
            uint8_t  inline_data[0x72]; /* 0xB0: the copy target when the
                                         *       data fits in the request */
        } __attribute__((packed)) list;
        /* 0x3C SERVER_OP_DIR_GET_ENTRY (0x00E61618) and
         * 0x3E SERVER_OP_DIR_READ_LINK (0x00E616BC) */
        struct {
            uint16_t dest_off;      /* 0x8E: added to 0x96 to get the copy
                                     *       target (0x00E6163E) */
            uint16_t data_len;      /* 0x90 */
            uint32_t data_va;       /* 0x92 */
        } __attribute__((packed)) entry;
        /* 0x42 SERVER_OP_DIR_READ_DIR (0x00E61696) */
        struct {
            uint8_t  _pad_8e[8];    /* 0x8E */
            uint32_t reply_max;     /* 0x96: clamped to 0x400, UNSIGNED */
            uint32_t reply_va;      /* 0x9A */
        } __attribute__((packed)) read_dir;
    } tail;
} rem_file_$rn_op_buf_t;

_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, op_code) == 0x03, "rn_op_buf.op_code");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, re_sids) == 0x14, "rn_op_buf.re_sids");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, sids) == 0x28, "rn_op_buf.sids");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, proj_list) == 0x4C, "rn_op_buf.proj_list");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, proj_count) == 0x8C, "rn_op_buf.proj_count");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.list.data_va) == 0x8E, "rn_op_buf.list.data_va");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.list.data_len) == 0x92, "rn_op_buf.list.data_len");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.list.reply_va) == 0xAC, "rn_op_buf.list.reply_va");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.list.inline_data) == 0xB0, "rn_op_buf.list.inline_data");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.entry.dest_off) == 0x8E, "rn_op_buf.entry.dest_off");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.entry.data_len) == 0x90, "rn_op_buf.entry.data_len");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.entry.data_va) == 0x92, "rn_op_buf.entry.data_va");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.read_dir.reply_max) == 0x96, "rn_op_buf.read_dir.reply_max");
_Static_assert(__builtin_offsetof(rem_file_$rn_op_buf_t, tail.read_dir.reply_va) == 0x9A, "rn_op_buf.read_dir.reply_va");
_Static_assert(sizeof(rem_file_$rn_op_buf_t) == 0x122, "rn_op_buf size");

/*
 * rem_file_$rn_op_resp_t - the reply buffer REM_FILE_$RN_DO_OP is handed
 * (A0 = (0x14,A6)).  Only the status is named: it is tested as its LOW WORD
 * at +0x06 (`tst.w (0x6,A0)` at 0x00E61574 and 0x00E6159E) and written whole
 * at 0x00E61708.
 */
typedef struct rem_file_$rn_op_resp_t {
    uint8_t     head[4];            /* 0x00 */
    status_$t   status;             /* 0x04 */
} rem_file_$rn_op_resp_t;

_Static_assert(__builtin_offsetof(rem_file_$rn_op_resp_t, status) == 0x04, "rn_op_resp.status");

/*
 * The four DIR_$SERVER opcodes REM_FILE_$RN_DO_OP gives special treatment.
 * They are the same four REM_FILE_$SERVER has to juggle a netbuf for; see
 * rem_file/server.c's SERVER_OP_DIR_* names.
 */
#define REM_FILE_RN_OP_DIR_GET_ENTRY    0x3C
#define REM_FILE_RN_OP_DIR_READ_LINK    0x3E
#define REM_FILE_RN_OP_DIR_READ_DIR     0x42
#define REM_FILE_RN_OP_DIR_LIST         0x58

/*
 * REM_FILE_$RN_DO_OP - Remote network do operation
 *
 * Higher-level wrapper that prepares security context (SIDs, project lists)
 * before sending a remote file operation.
 *
 * @param addr_info     Address info for target node
 * @param op_buffer     Operation buffer with request data
 * @param fixed_len     Fixed portion length
 * @param response_size      Operation flags
 * @param response      Response buffer (status at offset +4)
 * @param received_len  Output: reply length.  Forwarded verbatim to
 *                      REM_FILE_$SEND_REQUEST's `received_len` argument
 *                      (0x00E616E4 `move.l (0x18,A6),-(SP)`), which writes a
 *                      word through it at 0x00E61288 (source-32ld).
 *
 * Original address: 0x00E61538
 */
void REM_FILE_$RN_DO_OP(void *addr_info, void *op_buffer, int16_t fixed_len,
                        uint16_t response_size, void *response,
                        uint16_t *received_len);

/*
 * Helper macro to check if current process has admin privileges
 * (tst.w (-0x2,A0,D1w*2) with A0 = 0xe7dacc, i.e. ACL_$SUPER_COUNT[PROC1_$CURRENT])
 */
#define REM_FILE_PROCESS_HAS_ADMIN() \
    (ACL_$SUPER_COUNT[PROC1_$CURRENT] > 0)

#endif /* REM_FILE_INTERNAL_H */
