/*
 * NETWORK - Internal Header
 *
 * Internal definitions and helper prototypes for the NETWORK subsystem.
 * This file should be included by all .c files within the network/ directory.
 */

#ifndef NETWORK_INTERNAL_H
#define NETWORK_INTERNAL_H

#include "network/network.h"
#include "sock/sock.h"   /* SOCK_$DATA */
#include "pkt/pkt.h"     /* pkt_$info_t */
#include "ec/ec.h"
#include "app/app.h"     /* app_$receive_rec_t */

/*
 * NETWORK_$LOOPBACK_FLAG - Loopback mode indicator
 *
 * If negative (bit 7 set), network operations should use the local node
 * as the destination instead of the specified address.
 *
 * Original address: 0xE24C44
 */
/* NETWORK_$LOOPBACK_FLAG is exported from network/network.h -- pkt/ uses it. */

/*
 * Network command codes
 */
#define NETWORK_CMD_RING_INFO 0x0E /* Get ring information */

/*
 * Network status codes (module 0x11): see network/network.h
 */

/*
 * Network table - maps network indices to network IDs
 *
 * The table has 64 entries (indices 1-63, with 0 being special/unused).
 * Each entry is 8 bytes:
 *   - 4 bytes: reference count (number of uses of this network)
 *   - 4 bytes: network ID (the actual network identifier)
 *
 * A network address is a longword and carries the network index in bits 4-9
 * of its HIGH word (bits 20-25 of the whole longword, mask 0x3F0 applied to
 * the high half).  Extract with NETWORK_GET_INDEX below.
 *
 * Data layout in m68k memory:
 *   0xE24934: refcount[0], refcount[1], ... (each 8 bytes apart)
 *   0xE24938: net_id[0], net_id[1], ...     (each 8 bytes apart)
 */
typedef struct network_table_entry_t {
  uint32_t refcount; /* Number of references to this network */
  uint32_t net_id;   /* Network identifier */
} network_table_entry_t;

/*
 * The table is indexed 1..64: NETWORK_$INSTALL_NET runs "moveq #0x3f,D2 /
 * moveq #0x1,D3 / ... / dbf D2w" (0x00E0F204-0x00E0F240), 64 iterations with
 * the index starting at 1, and reaches slot n at A5 + 0x38 + 8*n
 * (0x00E0F224 / 0x00E0F25A).  Slot 0 exists in storage - the base A5 + 0x38
 * IS slot 0's refcount - but is never used, so the C array carries 65 entries
 * and keeps the original's 1-based indexing.
 */
#define NETWORK_MAX_NET_INDEX 64
#define NETWORK_TABLE_SIZE (NETWORK_MAX_NET_INDEX + 1)

/* Network table - slots 1..64, slot 0 unused */
extern network_table_entry_t NETWORK_$NET_TABLE[NETWORK_TABLE_SIZE];

/*
 * Extract the network index from a network address value.
 *
 * NETWORK_$GET_NET masks the HIGH word of its 4-byte first argument:
 * "and.w (0x8,A6),D0w" at 0x00E0F2E4 with D0 preloaded with 0x3F0
 * (0x00E0F2DC), then "lsr.w #0x4" (0x00E0F2E8).  The argument really is a
 * longword - the only caller, ast_$force_activate_segment, pushes its own
 * longword parameter with "move.l (0xc,A6),-(SP)" at 0x00E021FA and separately
 * masks the low 20 bits of the same value (0x00E021E6).  So the index lives in
 * bits 20..25 of the longword, i.e. bits 4..9 of its high half.
 */
#define NETWORK_INDEX_MASK 0x3F0
#define NETWORK_INDEX_SHIFT 4
#define NETWORK_GET_INDEX(addr)                                                \
  ((uint16_t)(((uint16_t)((addr) >> 16) & NETWORK_INDEX_MASK) >>               \
              NETWORK_INDEX_SHIFT))

/*
 * Network globals
 */
extern int16_t NETWORK_$SERVICE_TIME; /* 0xE24C18 (A5+0x31C): map NETWORK_$SERVICE_TIME,
                                       * the extra wait added to every reply
                                       * timeout (was NETWORK_$RETRY_TIMEOUT) */

/*
 * NETWORK_$ZERO_PAGE_PA - 0xE24B9C (A5+0x2A0), no map symbol: the byte
 * address (ppn << 10) of a zeroed page.  NETWORK_$PAGE_SERVER takes a page
 * with WP_$CALLOC and zeroes it with AST_$PAGE_ZERO at start-up
 * (0x00E115C0-0x00E115F2); NETWORK_$READ_AHEAD copies from it to zero-fill
 * pages past the end of an object (0x00E100C4, 0x00E10150).
 */
extern uint32_t NETWORK_$ZERO_PAGE_PA;

/*
 * Unnamed cells of the NETWORK data segment (map "D E248FC NETWORK size =
 * 364", A5 = 0xE248FC) used by the two server processes.  The module is not
 * yet a MODULE_DATA block, so they are individual objects like the rest of
 * network_data.c; each carries its address and A5 offset.
 *
 * NETWORK_$SERVER_PKT_INFO  0xE248FC (+0x000): the pkt_$info_t template the
 *     page and request servers copy (0x1E bytes) into their frames at entry
 *     (`lea (A5),A0 / moveq #6 ... / move.w (A0)+`, 0x00E11556, 0x00E118EA).
 *     Image: 00 08 00 02 00 02 80 31 ff ff 00 00 ff ff, then zeroes.
 * NETWORK_$RQST_DONE_EC     0xE24B54 (+0x258): advanced by the request
 *     server as it exits (0x00E11E02).
 * NETWORK_$RQST_QUIT_EC     0xE24B64 (+0x268): the request server's fifth
 *     wait eventcount; its advance makes the server exit (case 4).
 *     Both are EC_$INIT'd in the image (self-pointing waiter lists).
 * NETWORK_$RQST_WAIT[4]     0xE24B74 (+0x278): the request server's next
 *     wait values for sockets 2, 4, 8 and the clock.
 * NETWORK_$PAGE_WAIT[4]     0xE24B8C (+0x290): the page servers' next wait
 *     values for socket 6, socket 1, NETLOG_$EC and the clock.
 * NETWORK_$RING_DCTE        0xE24BA4 (+0x2A8): VA of the ring controller's
 *     DCTE, 0 without a ring; handed by reference to
 *     RING_$POLL_STICKY_BPHERR (0x00E11802).
 * NETWORK_$AGE_TICKS        0xE24C1A (+0x31E): clock ticks seen by the
 *     request server; every 4th minute boundary it runs the ring check.
 * NETWORK_$STD_OPEN_FLAG    0xE24C5E (+0x362): TRUE (0xFF in the image)
 *     until the request server has run RIP_$STD_OPEN / APP_$STD_OPEN.  It
 *     lies inside the four bytes the map gives NETWORK_$CLEAR_WIRED
 *     (0xE24C5C, next symbol 0xE24C60).
 */
extern pkt_$info_t NETWORK_$SERVER_PKT_INFO;
extern ec_$eventcount_t NETWORK_$RQST_DONE_EC;
extern ec_$eventcount_t NETWORK_$RQST_QUIT_EC;
extern uint32_t NETWORK_$RQST_WAIT[4];
extern uint32_t NETWORK_$PAGE_WAIT[4];
extern uint32_t NETWORK_$RING_DCTE;
extern uint16_t NETWORK_$AGE_TICKS;
extern int8_t NETWORK_$STD_OPEN_FLAG;

/*
 * NETWORK_$REPORT_SEND_FLAGS - 0xE24C58 (A5+0x35C), no map symbol (inside
 * the 16 bytes after NETWORK_$DISKLESS); image value 0x0001.  The flags
 * word NETWORK_$REPORT_FAILURE hands NET_IO_$SEND (0x00E104CC).
 */
extern uint16_t NETWORK_$REPORT_SEND_FLAGS;

/*
 * NETWORK_$REPLY_SEND_FLAGS - 0xE24C5A (A5+0x35E), no map symbol; image
 * value 0x0000.  The flags word the page server's reply sender hands
 * NET_IO_$SEND (0x00E105CC).
 * NETWORK_$FILE_OVER_CNT - 0xE24C20 (A5+0x324), map symbol: counts the
 * requests the page server refused on its overflow socket 6 (0x00E117F6).
 */
extern uint16_t NETWORK_$REPLY_SEND_FLAGS;
extern uint16_t NETWORK_$FILE_OVER_CNT;

/*
 * network_$rcv_rec_t - the record APP_$RECEIVE's reply pointer names for a
 * page-server request (app_$receive_rec_t.reply).  Only the fields
 * NETWORK_$PAGE_SERVER (0x00E1173C-0x00E11776) and
 * NETWORK_$PROCESS_PAGING_REQUEST (0x00E106CE-0x00E1070C) read are named;
 * word-aligned longwords, so packed.  The reply swaps the two ends: the
 * reply's source is +0x08/+0x0C and its destination +0x0E/+0x12.
 */
typedef struct network_$rcv_rec_t {
    uint16_t    _00;
    uint16_t    rqst_len;       /* 0x02: request bytes after this record */
    int16_t     data_len;       /* 0x04 */
    int16_t     request_id;     /* 0x06 */
    uint32_t    node_a;         /* 0x08 -> the reply's source node */
    int16_t     sock_a;         /* 0x0C -> the reply's source socket */
    uint32_t    node_b;         /* 0x0E -> the reply's destination node */
    int16_t     sock_b;         /* 0x12 -> the reply's destination socket */
    uint8_t     flags;          /* 0x14 -> the reply's packet flags */
} __attribute__((packed)) network_$rcv_rec_t;

_Static_assert(offsetof(network_$rcv_rec_t, rqst_len) == 0x02, "rcv_rec.rqst_len");
_Static_assert(offsetof(network_$rcv_rec_t, node_a) == 0x08, "rcv_rec.node_a");
_Static_assert(offsetof(network_$rcv_rec_t, node_b) == 0x0E, "rcv_rec.node_b");
_Static_assert(offsetof(network_$rcv_rec_t, flags) == 0x14, "rcv_rec.flags");

/*
 * network_$ps_frame_t - NETWORK_$PAGE_SERVER's frame (link.w A6,-0x384), the
 * record its nested procedures reach through the static link: the reply
 * sender 0x00E10510 (A1 = the server's A6) and
 * NETWORK_$PROCESS_PAGING_REQUEST 0x00E10628 (`movea.l (A6),A2`).  The
 * struct starts at A6-0x384; each field's comment gives its A6
 * displacement, and offset = 0x384 + displacement.  Regions no emitted code
 * names yet are opaque.  The server's eventcount pointer array (A6-0x70)
 * holds host pointers and is kept outside the struct (_70 is its slot).
 * Pointer-free, so the asserts are unconditional.
 */
typedef struct network_$ps_frame_t {
    uint8_t     _384[0x0A];
    int16_t     reply_len;      /* -0x37A: reply template length */
    int16_t     dest_sock;      /* -0x378 */
    int16_t     request_id;     /* -0x376 */
    uint16_t    rqst_copy_len;  /* -0x374 */
    int16_t     data_len;       /* -0x372: reply data length */
    int16_t     src_sock;       /* -0x370 */
    uint16_t    pkt_flags;      /* -0x36E */
    uint8_t     _36c[0x0C];
    uint16_t    failure_word;   /* -0x360: NETWORK_$REPORT_FAILURE's word */
    uint8_t     _35e[0x06];
    ml_$spin_token_t token;     /* -0x358 */
    uint16_t    _356;
    uint32_t    src_node;       /* -0x354 */
    uint32_t    src_node_or;    /* -0x350 */
    uint32_t    dest_node;      /* -0x34C */
    uint32_t    routing_key;    /* -0x348 */
    uint32_t    hdr_pa;         /* -0x344 */
    uint32_t    zero_ppn;       /* -0x340 */
    status_$t   status;         /* -0x33C */
    uint8_t     _338[0x08];
    uint32_t    deadline;       /* -0x330: compared by the reply sender */
    uint8_t     _32c[0x08];
    uint32_t    arrival;        /* -0x324: app_$receive_rec_t.src_addr */
    uint8_t     _320[0x24];
    uint32_t    rtn_hdr_va;     /* -0x2FC */
    uint8_t     overflow_rqst[0xB8]; /* -0x2F8 */
    uint32_t    data_pages[4];  /* -0x240 */
    uint8_t     rqst[0x48];     /* -0x230 */
    uint8_t     reply[0xB8];    /* -0x1E8 */
    uint8_t     _130[0xC0];
    uint8_t     _70[0x10];      /* -0x070: eventcount pointers (see above) */
    uint32_t    waits[4];       /* -0x060 */
    pkt_$info_t pkt_info;       /* -0x050: NETWORK_$SERVER_PKT_INFO copy */
    app_$receive_rec_t rec;     /* -0x030 */
    uint8_t     _04[0x04];
} network_$ps_frame_t;

#define NETWORK_PS_OFF(disp)    (0x384 + (disp))
_Static_assert(offsetof(network_$ps_frame_t, reply_len) == NETWORK_PS_OFF(-0x37A), "ps.reply_len");
_Static_assert(offsetof(network_$ps_frame_t, pkt_flags) == NETWORK_PS_OFF(-0x36E), "ps.pkt_flags");
_Static_assert(offsetof(network_$ps_frame_t, failure_word) == NETWORK_PS_OFF(-0x360), "ps.failure_word");
_Static_assert(offsetof(network_$ps_frame_t, token) == NETWORK_PS_OFF(-0x358), "ps.token");
_Static_assert(offsetof(network_$ps_frame_t, src_node) == NETWORK_PS_OFF(-0x354), "ps.src_node");
_Static_assert(offsetof(network_$ps_frame_t, status) == NETWORK_PS_OFF(-0x33C), "ps.status");
_Static_assert(offsetof(network_$ps_frame_t, deadline) == NETWORK_PS_OFF(-0x330), "ps.deadline");
_Static_assert(offsetof(network_$ps_frame_t, arrival) == NETWORK_PS_OFF(-0x324), "ps.arrival");
_Static_assert(offsetof(network_$ps_frame_t, rtn_hdr_va) == NETWORK_PS_OFF(-0x2FC), "ps.rtn_hdr_va");
_Static_assert(offsetof(network_$ps_frame_t, overflow_rqst) == NETWORK_PS_OFF(-0x2F8), "ps.overflow_rqst");
_Static_assert(offsetof(network_$ps_frame_t, data_pages) == NETWORK_PS_OFF(-0x240), "ps.data_pages");
_Static_assert(offsetof(network_$ps_frame_t, rqst) == NETWORK_PS_OFF(-0x230), "ps.rqst");
_Static_assert(offsetof(network_$ps_frame_t, reply) == NETWORK_PS_OFF(-0x1E8), "ps.reply");
_Static_assert(offsetof(network_$ps_frame_t, waits) == NETWORK_PS_OFF(-0x060), "ps.waits");
_Static_assert(offsetof(network_$ps_frame_t, pkt_info) == NETWORK_PS_OFF(-0x050), "ps.pkt_info");
_Static_assert(offsetof(network_$ps_frame_t, rec) == NETWORK_PS_OFF(-0x030), "ps.rec");
_Static_assert(sizeof(network_$ps_frame_t) == 0x384, "ps frame: link.w A6,-0x384");

/*
 * NETWORK_$PROCESS_PAGING_REQUEST (0x00E10628, 3842 bytes) - the page
 * server's nested procedure for a request on socket 1 (0x00E11708), which
 * reaches the server's frame through `movea.l (A6),A2`.  No map symbol.
 * TODO(source-590f): not yet emitted; it belongs in network/page_server.c
 * as a static taking the frame, like network_$ps_send_reply.
 */
void NETWORK_$PROCESS_PAGING_REQUEST(network_$ps_frame_t *ps);

/*
 * network_$c_zero_long - the by-reference zero longword at 0x00E1050C
 * (NETWORK_$REPORT_FAILURE's constant pool), shared by
 * NETWORK_$REPORT_FAILURE and NETWORK_$REQUEST_SERVER.  Defined in
 * network/report_failure.c.
 */
extern const uint32_t network_$c_zero_long;

/* The template length the servers copy: 7 longwords and a word */
#define NETWORK_SERVER_PKT_INFO_LEN 0x1E

/*
 * network_$pagin_rqst_t - the 0x2E-byte page-in request NETWORK_$READ_AHEAD
 * builds at A6-0x110 (0x00E0FCEE-0x00E0FD1A) and hands network_$send_request
 * with length 0x2E.  The caller's 32-byte record sits at +6, so its longwords
 * are only word aligned: packed.
 */
typedef struct network_$pagin_rqst_t {
    int16_t     type;           /* 0x00: 0x000C, `move.l #0xc0008,(-0x110,A6)` */
    int16_t     version;        /* 0x02: 8 */
    int16_t     limit;          /* 0x04: most pages per request (0x20 or 8) */
    network_$page_request_t req;/* 0x06: the caller's record; req.page_num is
                                 *       rewritten per request (0x00E0FD02) */
    int8_t      flag_1a;        /* 0x26: READ_AHEAD's (0x1A,A6) byte */
    uint8_t     _27;
    uint16_t    count;          /* 0x28: pages wanted, end - start (low words) */
    int8_t      flag_18;        /* 0x2A: READ_AHEAD's (0x18,A6) byte */
    uint8_t     _2b;
    uint16_t    page_size;      /* 0x2C: 0x400 */
} __attribute__((packed)) network_$pagin_rqst_t;

_Static_assert(offsetof(network_$pagin_rqst_t, req) == 0x06, "pagin_rqst.req");
_Static_assert(offsetof(network_$pagin_rqst_t, flag_1a) == 0x26, "pagin_rqst.flag_1a (-0xea)");
_Static_assert(offsetof(network_$pagin_rqst_t, count) == 0x28, "pagin_rqst.count (-0xe8)");
_Static_assert(offsetof(network_$pagin_rqst_t, flag_18) == 0x2A, "pagin_rqst.flag_18 (-0xe6)");
_Static_assert(offsetof(network_$pagin_rqst_t, page_size) == 0x2C, "pagin_rqst.page_size (-0xe4)");
_Static_assert(sizeof(network_$pagin_rqst_t) == 0x2E, "pagin_rqst length 0x2E");

/*
 * network_$pagin_reply_t - the reply NETWORK_$READ_AHEAD receives at
 * A6-0xC8 (network_$wait_response fills it; the buffer runs to A6-0x10,
 * where the data-buffer array begins).  Offsets from the reads at
 * 0x00E0FEA2-0x00E0FF72 and 0x00E1007A.  The status longword is at +2:
 * packed, like network_$reply_hdr_t.
 */
typedef struct network_$pagin_reply_t {
    int16_t     type;           /* 0x00: 0x000D = request type + 1 */
    status_$t   status;         /* 0x02 */
    int16_t     version;        /* 0x06: >= 7 carries the three clocks */
    int16_t     seq;            /* 0x08: packet number within the reply, 1.. */
    int16_t     page_cnt;       /* 0x0A: packets (pages) in the reply */
    network_$page_request_t req;/* 0x0C: copied back to the caller once */
    uint32_t    chksum;         /* 0x2C: 0 = none */
    int16_t     more;           /* 0x30: negative = the server stopped early */
    uint16_t    _32;
    uint32_t    dtm_high;       /* 0x34 -> *dtm (long, word) */
    uint16_t    dtm_low;        /* 0x38 */
    uint16_t    _3a;
    uint32_t    clock_high;     /* 0x3C -> *clock */
    uint16_t    clock_low;      /* 0x40 */
    uint16_t    _42;
    uint32_t    acl_high;       /* 0x44 -> *acl_info */
    uint16_t    acl_low;        /* 0x48 */
    uint8_t     _4a[0x6E];      /* 0x4A .. 0xB7 */
} __attribute__((packed)) network_$pagin_reply_t;

_Static_assert(offsetof(network_$pagin_reply_t, status) == 0x02, "pagin_reply.status (-0xc6)");
_Static_assert(offsetof(network_$pagin_reply_t, version) == 0x06, "pagin_reply.version (-0xc2)");
_Static_assert(offsetof(network_$pagin_reply_t, seq) == 0x08, "pagin_reply.seq (-0xc0)");
_Static_assert(offsetof(network_$pagin_reply_t, page_cnt) == 0x0A, "pagin_reply.page_cnt (-0xbe)");
_Static_assert(offsetof(network_$pagin_reply_t, req) == 0x0C, "pagin_reply.req (-0xbc)");
_Static_assert(offsetof(network_$pagin_reply_t, chksum) == 0x2C, "pagin_reply.chksum (-0x9c)");
_Static_assert(offsetof(network_$pagin_reply_t, more) == 0x30, "pagin_reply.more (-0x98)");
_Static_assert(offsetof(network_$pagin_reply_t, dtm_high) == 0x34, "pagin_reply.dtm (-0x94)");
_Static_assert(offsetof(network_$pagin_reply_t, clock_high) == 0x3C, "pagin_reply.clock (-0x8c)");
_Static_assert(offsetof(network_$pagin_reply_t, acl_high) == 0x44, "pagin_reply.acl (-0x84)");
_Static_assert(sizeof(network_$pagin_reply_t) == 0xB8, "pagin_reply: A6-0xC8 .. A6-0x10");

/*
 * NETWORK_$LOCK - Spin lock for network data protection
 *
 * Located at network data base + 0x2A4 = 0xE24BA0
 */
extern void *NETWORK_$LOCK;

/* The socket pointer table is SOCK_$DATA.socket_ptr (sock/sock.h). */

/*
 * network_$reply_hdr_t - the head of the reply buffer network_$do_request
 * validates.
 *
 * 0x00E0F9CC-0x00E0F9E6, with A4 = the reply buffer and A0 = the request:
 *   move.w (A4),D0w / ext.l D0
 *   move.w (A0),D1w / ext.l D1 / addq.l #0x1,D1
 *   cmp.l D1,D0 / bne -> status_$network_unexpected_reply_type
 *   move.l (0x2,A4),(A3)        the reply's own status
 * The longword sits on an odd word boundary, so the record is packed.
 */
typedef struct network_$reply_hdr_t {
    int16_t     reply_type;     /* 0x00: must be request_type + 1 */
    status_$t   status;         /* 0x02: UNALIGNED longword */
} __attribute__((packed)) network_$reply_hdr_t;

_Static_assert(offsetof(network_$reply_hdr_t, status) == 0x02,
               "network_$reply_hdr_t.status");
_Static_assert(sizeof(network_$reply_hdr_t) == 6,
               "network_$reply_hdr_t must be 6 bytes");

/*
 * network_$send_request - Send a network request packet
 *
 * Internal helper that builds and sends a network request packet.
 * Handles retries on transmission failure.
 *
 * @param net_handle       Network handle
 * @param sock_num         Socket number
 * @param pkt_id           Packet ID
 * @param cmd_buf          Command buffer
 * @param cmd_len          Command length
 * @param param_hi         High word of param4
 * @param param_lo         Combined param4_lo and param5
 * @param retry_count_out  Output: max retry count
 * @param timeout_out      Output: timeout value
 * @param status_ret       Output: status code
 *
 * Original address: 0x00E0F5F4
 */
void network_$send_request(void *net_handle, int16_t sock_num, int16_t pkt_id,
                           int16_t *cmd_buf, int16_t cmd_len, int16_t param_hi,
                           uint32_t param_lo, uint16_t *retry_count_out,
                           int16_t *timeout_out, status_$t *status_ret);

/*
 * network_$wait_response - Wait for network response
 *
 * Internal helper that waits for a response packet matching the given
 * packet ID. Uses event count waiting for efficient blocking.
 *
 * @param sock_num         Socket number
 * @param pkt_id           Packet ID to match
 * @param timeout          Timeout in clock ticks
 * @param event_count      Event count pointer (updated on each iteration)
 * @param resp_buf         Response buffer output
 * @param resp_len_out     Output: response length
 * @param data_bufs        Output: data buffer pointers
 * @param data_len_out     Output: data length
 *
 * @return Negative (0xFF) on success, 0 on timeout
 *
 * Original address: 0x00E0F746
 */
int8_t network_$wait_response(int16_t sock_num, int16_t pkt_id,
                              uint16_t timeout, int32_t *event_count,
                              int16_t *resp_buf, int16_t *resp_len_out,
                              uint32_t *data_bufs, uint16_t *data_len_out);

/*
 * network_$do_request - Send a network command and receive response
 *
 * Internal helper function that sends a command to a network partner
 * and waits for a response. Handles packet allocation, transmission,
 * and response collection.
 *
 * @param net_handle     Network handle/connection
 * @param cmd_buf        Command buffer to send
 * @param cmd_len        Command length (in bytes)
 * @param param4         Reserved (pass 0)
 * @param param5         Reserved (pass 0)
 * @param param6         Reserved (pass 0)
 * @param resp_buf       Response buffer
 * @param resp_info      Response info output
 * @param status_ret     Output: status code
 *
 * Original address: 0x00E0F86C
 */
void network_$do_request(void *net_handle, void *cmd_buf, int16_t cmd_len,
                         uint32_t param4, uint16_t param5, int16_t param6,
                         void *resp_buf, void *resp_info,
                         status_$t *status_ret);

/*
 * network_$phys_copy (0x00E0F120, was FUN_00e0f120) - copy `len` bytes from
 * physical byte address src_pa to dst_pa through the AST_$COPY_BUFF /
 * AST_$ZERO_BUFF windows under ML lock 0x14.  Emitted in network/phys_copy.c.
 */
void network_$phys_copy(uint32_t dst_pa, uint32_t src_pa, int16_t len);

/*
 * network_$page_chksum (0x00E0F312, map GET_CHKSUM) - NETWORK_$GET_CHKSUM of
 * the page whose physical address is *data_bufs.  Emitted in
 * network/page_chksum.c.
 */
uint32_t network_$page_chksum(uint32_t *data_bufs);

#endif /* NETWORK_INTERNAL_H */
