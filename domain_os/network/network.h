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
 *
 * This is the single home of the status_$network_* codes; other
 * subsystems (asknode, ring, rip, route, ...) include this header.
 */
/*
 * The texts below are the ones the SR10.4 status-code database ships for
 * module 0x11 ("OS / network"), so these names are the original meanings and
 * not guesses.  Codes the kernel sources here do not (yet) reference are
 * listed as well so that the block reads as the original table did.
 */
#define status_$network_buffer_error                    0x00110001
#define status_$network_out_of_pages                    0x00110002
#define status_$network_out_of_blocks                   0x00110003
#define status_$network_transmit_failed                 0x00110004
#define status_$network_receive_process_failed_to_start 0x00110005
#define status_$network_buffer_queue_is_empty           0x00110006
#define status_$network_remote_node_failed_to_respond   0x00110007
#define status_$network_unable_to_route                 0x00110008
#define status_$network_hardware_error                  0x00110009
#define status_$network_msg_header_too_big              0x0011000A
#define status_$network_unexpected_reply_type           0x0011000B
#define status_$network_no_more_free_sockets            0x0011000C
#define status_$network_unknown_request_type            0x0011000D
#define status_$network_request_denied_by_local_node    0x0011000E
#define status_$network_request_denied_by_remote_node   0x0011000F
#define status_$network_bad_checksum                    0x00110010
#define status_$network_too_many_transmit_retries       0x00110011
#define status_$network_socket_not_open                 0x00110012
#define status_$network_receive_bus_error               0x00110013
#define status_$network_transmit_bus_error              0x00110014
#define status_$network_bad_asknode_version_number      0x00110015
#define status_$network_memory_parity_error_during_transmit 0x00110016
#define status_$network_unknown_network                 0x00110017
#define status_$network_too_many_networks_in_internet   0x00110018
#define status_$network_conflict_with_another_node_listing 0x00110019
#define status_$network_quit_fault_during_node_listing  0x0011001A
#define status_$network_waited_too_long_for_more_node_responses 0x0011001B
#define status_$network_data_length_too_large           0x0011001C
#define status_$network_operation_not_defined_on_hardware 0x0011001D
#define status_$network_msg_exceeds_max_size            0x0011001E
#define status_$network_no_nodeid_prom_on_this_system   0x0011001F
#define status_$network_device_stat_block_not_valid     0x00110020
#define status_$network_device_stat_index_out_of_range  0x00110021
#define status_$network_foreign_node_missing_features   0x00110022
#define status_$network_transmit_with_invalid_from_id   0x00110023
#define status_$network_header_data_length_exceeds_max  0x00110024
#define status_$network_extended_service_delay          0x00110025
#define status_$network_server_out_of_queued_buffers    0x00110026

/*
 * Network service flags.
 *
 * These are bits of the SERVICE WORD, which is the HIGH half of the
 * NETWORK_$ALLOWED_SERVICE longword at 0xE24C3E: every access in the image
 * is `move.w ...,(0x342,A5)` or `btst.b #n,(0x343,A5)` with A5 = 0xE248FC,
 * and 0x343 is byte 1 of that longword.  As a longword bit number, flag bit
 * n is bit n+16.
 *
 *   Bit 0 (0x01): Paging service enabled
 *   Bit 1 (0x02): File service enabled
 *   Bit 2 (0x04): Network service active
 *   Bit 3 (0x08): Routing enabled (auto-set if routing ports exist)
 *   Bit 4 (0x10): Reserved
 */
#define NETWORK_SERVICE_PAGING      0x0001
#define NETWORK_SERVICE_FILE        0x0002
#define NETWORK_SERVICE_ACTIVE      0x0004
#define NETWORK_SERVICE_ROUTING     0x0008
#define NETWORK_SERVICE_RESERVED_4  0x0010

/*
 * NETWORK_SERVICE_EXTENDED as a longword mask: NETWORK_$READ_SERVICE tests it
 * with `btst.b #0x2,(0x343,A1)` (0x00E71D8A), i.e. NETWORK_SERVICE_ACTIVE of
 * the service word, which is bit 18 of the longword.
 */
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
/*
 * NETWORK_$ALLOWED_SERVICE - one longword at 0xE24C3E (A5+0x342) covering
 * two independently written halves:
 *
 *   bits 16..31  the service word (NETWORK_SERVICE_* above); its low byte,
 *                0xE24C3F, is NETWORK_$CAPABLE_FLAGS
 *   bits  0..15  NETWORK_$REMOTE_POOL, the remote buffer pool size that
 *                NETWORK_$SET_SERVICE op 3 stores at A5+0x344 (0x00E0F526)
 *
 * NETWORK_$READ_SERVICE hands the whole longword out when the service is
 * active (`move.l (0x342,A1),(A0)` at 0x00E71D92), which is why the two
 * halves must share storage rather than being separate variables.
 */
extern uint32_t NETWORK_$ALLOWED_SERVICE;    /* 0xE24C3E (+0x342) - 32-bit */

/* The service word, bits 16..31 of the longword above. */
#define NETWORK_$SERVICE_FLAGS  ((uint16_t)(NETWORK_$ALLOWED_SERVICE >> 16))
#define NETWORK_$SET_SERVICE_FLAGS(w)                                        \
    (NETWORK_$ALLOWED_SERVICE = (NETWORK_$ALLOWED_SERVICE & 0x0000FFFFu) |   \
                                ((uint32_t)(uint16_t)(w) << 16))

/* The remote pool size, bits 0..15 (0xE24C40, A5+0x344). */
#define NETWORK_$REMOTE_POOL    ((int16_t)(uint16_t)NETWORK_$ALLOWED_SERVICE)
#define NETWORK_$SET_REMOTE_POOL(w)                                          \
    (NETWORK_$ALLOWED_SERVICE = (NETWORK_$ALLOWED_SERVICE & 0xFFFF0000u) |   \
                                (uint32_t)(uint16_t)(w))
extern int8_t NETWORK_$ACTIVITY_FLAG;        /* 0xE24C42 (+0x346) */
extern char NETWORK_$DO_CHKSUM;
/*
 * Loopback flag at 0xE24C44.  If negative (bit 7 set), network operations use
 * the local node as the destination.  Moved here from network_internal.h
 * because pkt/ references it (bead source-3uo).
 */
extern int8_t NETWORK_$LOOPBACK_FLAG;        /* 0xE24C44 (+0x348) */
extern int8_t NETWORK_$USER_SOCK_OPEN;       /* 0xE24C48 (+0x34C) */
extern int8_t NETWORK_$REALLY_DISKLESS;      /* 0xE24C4A (+0x34E) */
extern int8_t NETWORK_$DISKLESS;             /* 0xE24C4C (+0x350) - diskless mode */
extern uid_t NETWORK_$PAGING_FILE_UID;

/*
 * Network statistics.
 *
 * Both backlog symbols name a nine-longword histogram, not a single counter:
 * NETWORK_$PAGING_BACKLOG runs 0xE24BAC..0xE24BCF and NETWORK_$FILE_BACKLOG
 * 0xE24BD0..0xE24BF3, ending exactly where NETWORK_$FAILURE_REC begins, and
 * ASKNODE_$INTERNET_INFO's request-0x29 arm copies nine longwords out of each
 * ("moveq #0x8,D3 / move.l (A0)+,(A2)+ / dbf" at 0x00E64DEE and 0x00E64E08).
 */
#define NETWORK_PAGING_BACKLOG_BUCKETS  9
extern uint32_t NETWORK_$PAGING_BACKLOG[NETWORK_PAGING_BACKLOG_BUCKETS]; /* 0xE24BAC */

/*
 * 0xE24BD0 is the base of a nine-entry histogram of the file-server request
 * backlog; REM_FILE_$SERVER indexes it with the depth byte at +0x15 of the
 * record NETWORK_$SERVICE_INFO_PTR points at, and counts everything deeper
 * than eight in the overflow cell (0x00E63628-0x00E6364E).
 */
#define NETWORK_FILE_BACKLOG_BUCKETS    9
extern uint32_t NETWORK_$FILE_BACKLOG[NETWORK_FILE_BACKLOG_BUCKETS];  /* 0xE24BD0 */
/*
 * The overflow cell is the last bucket (0xE24BD0 + 8*4 = 0xE24BF0); the SAU2
 * link map has no separate symbol there, so it is spelled as that bucket
 * rather than as storage of its own.
 */
#define NETWORK_$FILE_BACKLOG_OVERFLOW  (NETWORK_$FILE_BACKLOG[8])
extern uint8_t *NETWORK_$SERVICE_INFO_PTR;       /* 0xE28DB8 */
extern uint16_t NETWORK_$RCV_READ_AHEAD;      /* 0xE24C26 */

/*
 * NETWORK_$2LONG1 (0xE245A2) - one of the four cells of the NET_ASM assembly data
 * module (SAU2 map: `D E2459C NET_ASM size = C`, holding HINT_$HINTFILE_PTR,
 * REM_FILE_$2LONG1, NETWORK_$2LONG1 and NODE_$ME).  It is a word counter:
 * ASKNODE_$INTERNET_INFO's request-0x29 arm reports it with move.w
 * (0x00E64E2E).
 * TODO: recover what the counter counts (bead source-t74x).
 */
extern uint16_t NETWORK_$2LONG1;

extern uint16_t NETWORK_$MULT_PAGIN_RQST_CNT; /* 0xE24C28 */
extern uint16_t NETWORK_$BAD_CHKSUM_CNT;      /* 0xE24C2A */
extern uint16_t NETWORK_$READ_VIOL_CNT;       /* 0xE24C2C */
extern uint16_t NETWORK_$WRITE_VIOL_CNT;      /* 0xE24C2E */
extern uint16_t NETWORK_$READ_CALL_CNT;       /* 0xE24C30 */
extern uint16_t NETWORK_$WRITE_CALL_CNT;      /* 0xE24C32 */
extern uint16_t NETWORK_$SET_ATTRIB_CALL_CNT; /* 0xE24C34 */
extern uint16_t NETWORK_$ATTRIB_RQST_CNT;     /* 0xE24C36 */
extern uint16_t NETWORK_$INFO_RQST_CNT;       /* 0xE24C38 */
extern uint16_t NETWORK_$PAGIN_RQST_CNT;      /* 0xE24C3A */
extern uint16_t NETWORK_$PAGOUT_RQST_CNT;     /* 0xE24C3C */

/*
 * NETWORK_$CAPABLE_FLAGS - Network capability flags (bit 0 = network capable)
 *
 * Original address: 0xE24C3F, which is byte 1 of the NETWORK_$ALLOWED_SERVICE
 * longword at 0xE24C3E: bit n here is bit (16 + n) there.  It is therefore
 * not storage of its own but a view of that longword, spelled here as a shift
 * so that it reads the same bits on any byte order.
 *
 * Read-only: no code in the image writes the byte.  NETWORK_$SET_SERVICE
 * (0x00E2F5FC) writes the whole longword; REM_FILE_$SEND_REQUEST
 * (0x00E63A3E) and REM_FILE_$SERVER (0x00E637FA) only test bits in it.
 */
#define NETWORK_$CAPABLE_FLAGS ((uint8_t)(NETWORK_$ALLOWED_SERVICE >> 16))

/* Bit 16 of NETWORK_$ALLOWED_SERVICE == bit 0 of NETWORK_$CAPABLE_FLAGS */
#define NETWORK_SERVICE_CAPABLE     0x00010000
#define NETWORK_SERVICE_FILE_CAPABLE 0x00020000

/*
 * NETWORK_$FAILURE_REC - Network failure record (16 bytes)
 *
 * Written by ASKNODE_$SERVER (request 0x0E) and read by
 * ASKNODE_$READ_FAILURE_REC.
 *
 * Original address: 0xE24BF4
 */
typedef struct network_$failure_rec_t {
    uint16_t    word0;          /* 0x00 */
    int8_t      flag;           /* 0x02: Pascal boolean - "st (0x2,A0)" at
                                 *       0x00E65D0A and 0x00E75F22 make it
                                 *       0xFF once a failure has been
                                 *       recorded; cleared when
                                 *       NETWORK_$ACTIVITY_FLAG < 0.  Test it
                                 *       with "< 0". */
    uint8_t     byte3;          /* 0x03 */
    /*
     * +0x04 is the node the failure is reported AGAINST and +0x0C is the
     * failure type.  All three writers agree (bead source-oowv):
     *
     *   NETWORK_$REPORT_FAILURE (0x00E103FA), A5 = 0x00E248FC and the record
     *   at A5+0x2F8:
     *     0x00E10414  move.l (0x00e245a4).l,(0x2fc,A5)  +0x04 <- NODE_$ME
     *     0x00E10408  move.l (0x00e2b0d4).l,(0x300,A5)  +0x08 <- TIME_$CLOCKH
     *     0x00E1042E  move.l D0,(0x304,A5)              +0x0C <- 1 or 3
     *
     *   ASKNODE_$SERVER request 0x0E (0x00E65D04):
     *     0x00E65D0E  move.l D6,(0x4,A0)                +0x04 <- requesting
     *                                                            node (D6,
     *                                                            0x00E659D8)
     *     0x00E65D1A  move.l (-0x264,A6),(0xc,A0)       +0x0C <- request word
     *
     *   ring_$validate_receive (0x00E75DE4), A0 = 0x00E24BF4:
     *     0x00E75F1C  move.l (0x8,A4),(0x4,A0)          +0x04 <- hdr->src_id
     *     0x00E75F2A  move.l (A4),(0xc,A0)              +0x0C <- hdr->msg_type
     *                                                            (1 or 3)
     *
     * The name "failure type" is netmain's own: its hardware-failure display
     * prints "v<n>  Failure type = <lh>" and "reported by <node> at <time>".
     */
    uint32_t    node_id;        /* 0x04: node the failure is reported against */
    uint32_t    timestamp;      /* 0x08: TIME_$CURRENT_CLOCKH at failure */
    uint32_t    failure_type;   /* 0x0C: 1 or 3 - netmain's "Failure type" */
} network_$failure_rec_t;

_Static_assert(sizeof(network_$failure_rec_t) == 0x10,
               "network_$failure_rec_t must be 16 bytes");
_Static_assert(offsetof(network_$failure_rec_t, node_id) == 0x04,
               "network_$failure_rec_t.node_id (0x00E10414/0x00E65D0E/0x00E75F1C)");
_Static_assert(offsetof(network_$failure_rec_t, timestamp) == 0x08,
               "network_$failure_rec_t.timestamp (0x00E10408/0x00E65D12)");
_Static_assert(offsetof(network_$failure_rec_t, failure_type) == 0x0C,
               "network_$failure_rec_t.failure_type (0x00E1042E/0x00E65D1A/0x00E75F2A)");

extern network_$failure_rec_t NETWORK_$FAILURE_REC;

/*
 * Note: ROUTE_$N_ROUTING_PORTS is declared in route/route.h
 * Include that header if you need access to it.
 */

/*
 * network_$page_request_t - the 32-byte IN/OUT record NETWORK_$READ_AHEAD
 * takes as its second argument.
 *
 * Recovered from the two 8-longword copies in NETWORK_$READ_AHEAD
 * (0xE0FC8A "moveq #0x7 / move.l (A0)+,(A1)+ / dbf" and the matching copy
 * back at 0xE0FF72) plus the read of +8 at 0xE0FC9E.  Only the first twelve
 * bytes are initialised by ast_$read_area_pages_network (0xE02D20 and
 * 0xE02D3C); the remaining 20 are whatever was on its stack.
 */
typedef struct network_$page_request_t {
    uid_t    uid;           /* 0x00: object UID */
    uint32_t page_num;      /* 0x08: first page requested (0xE0FC9E) */
    uint32_t reserved[5];   /* 0x0C: uninitialised by the caller */
} network_$page_request_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(network_$page_request_t, page_num) == 0x08,
               "network_$page_request_t.page_num");
_Static_assert(sizeof(network_$page_request_t) == 0x20,
               "network_$page_request_t must be 32 bytes (8 longwords)");
#endif

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
 * @param dtm            DTM output   (0xE0FF64: long at +0, word at +4)
 * @param clock          Clock output (0xE0FF48: long at +0, word at +4)
 * @param acl_info       ACL/DTA output (0xE0FF56: long at +0, word at +4)
 * @param status         Output status code
 *
 * All three timestamp outputs are 48-bit clock_t records: the callee writes
 * "move.l Dn,(A0) / move.w Dn,(0x4,A0)" to each of (0x1c,A6), (0x20,A6) and
 * (0x24,A6) at 0xE0FF48-0xE0FF6C.  (bead source-hz1)
 *
 * "uid" is a 32-byte IN/OUT request record, not an 8-byte UID: 0xE0FC8A
 * copies eight longwords out of it into the callee's frame and 0xE0FF72
 * copies eight longwords back.  Its first eight bytes are the object UID
 * and the longword at +8 is the starting page number (read at 0xE0FC9E).
 *
 * @return Number of pages successfully read
 */
int16_t NETWORK_$READ_AHEAD(void *net_info, void *uid, uint32_t *ppn_array,
                            uint16_t page_size, int16_t count,
                            int8_t no_read_ahead, uint8_t flags,
                            clock_t *dtm, clock_t *clock,
                            clock_t *acl_info, status_$t *status);

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
 * @param info    Address of the longword whose HIGH half is the network info
 *                word; bits 4-9 of that word (bits 20-25 of the longword)
 *                receive the network index.  0x00E0F1F2 takes it as a
 *                longword address and updates it with `andi.w`/`or.w` on
 *                (A0), i.e. the most significant half on m68k.
 * @param status  Output status code
 *
 * Original address: 0x00E0F1E0
 */
void NETWORK_$INSTALL_NET(uint32_t node, uint32_t *info, status_$t *status);

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
 * ring_info_t - Token Ring status record carried by ASKNODE request 0x1F
 *
 * 122 bytes (0x7A).  The size is pinned twice: NETWORK_$RING_INFO's copy loop
 * at 0x00E103E6..0x00E103EE (moveq #0x1d + dbf = 30 longs, then one word)
 * takes it out of the response packet at offset 6, and the responder --
 * NETWORK_$PROCESS_PAGING_REQUEST case 0x0E, 0x00E11246..0x00E112D4 -- fills exactly
 * 0x7A bytes at reply+6 and nothing beyond.
 *
 * The kernel never interprets the bytes on the receiving side, so the layout
 * comes from the *sending* side.  In this image both sides are present:
 *
 *   requester  ASKNODE_$INTERNET_INFO (0x00E645EA), request word 0x1F, calls
 *              NETWORK_$RING_INFO with the ASKNODE reply record + 8
 *              (pea (0x8,A1) at 0x00E6557E) and puts the network status at
 *              reply+4; the reply opcode is 0x20.
 *   responder  NETWORK_$PROCESS_PAGING_REQUEST case 0x0E, which sets the reply type
 *              word to 0x000F (0x00E11250), clears the reply status longword
 *              (0x00E1124C) and then builds this record at reply+6.
 *
 * Every field below cites the instruction in that responder that stores it.
 * A5 there is the network module base 0x00E248FC (proved by
 * "lea (0xe248fc).l,A5" at 0x00E10402 in NETWORK_$REPORT_FAILURE, which uses
 * the same A5 displacements for NETWORK_$FAILURE_REC).
 *
 * The field *names* are Apollo's own, from /etc/netmain in the SR10.4
 * distribution: its "Error counts for <node>" display formats this record and
 * names the counters (see ring_$stats_t in ring/ring.h, bead source-1a5o).
 * netmain shows 25 counters where ring_$stats_t holds 22 - the extra three,
 * "xmit bph", "rcv bph" and "xmit esb", are the standalone words at the tail
 * of this record, which is what identifies +0x72..+0x79.
 *
 * The record is a wire record: it is packed, and it embeds two copies of
 * records that live elsewhere.
 *
 *   +0x08 is a verbatim copy of ring_$stats_t for unit 0 (ring/ring.h).  It
 *         is spelled out field by field rather than embedded by type because
 *         ring/ring.h #includes this header for the status_$network_* codes,
 *         so this header cannot include ring/ring.h back.  ring/ring.h is the
 *         authority for those names and for the evidence behind them.
 *   +0x54 is a copy of RING_$SWDIAG_DATA (0x00E261C2), the software-diagnostic
 *         mirror of the receive counters, with two longwords patched in from
 *         the standalone globals that hold the live values.
 */
typedef struct ring_info_t {
    /* ---- header ---------------------------------------------------- */
    uint16_t    _unknown_00;        /* 0x00: always the constant 3
                                     *       ("move.w #0x3,(-0x1e2,A0)",
                                     *       0x00E112BC).  Other replies from
                                     *       the same dispatcher put an
                                     *       unrelated value in this slot
                                     *       (8 at 0x00E10A3C / 0x00E11172 /
                                     *       0x00E1148C / 0x00E1151E), so it is
                                     *       per-payload, not a shared header
                                     *       word.  TODO(source-4omb): name it. */
    int8_t      diskless;           /* 0x02: NETWORK_$DISKLESS (0x00E24C4C =
                                     *       A5+0x350), "move.b (0x350,A5),
                                     *       (-0x1e0,A0)" at 0x00E112C2.
                                     *       Pascal boolean, test it with "< 0".
                                     *       netmain: "<node> is diskless." /
                                     *       "<node> has a disk." */
    uint8_t     _unknown_03;        /* 0x03: never written by the responder */
    uint32_t    mother_node;        /* 0x04: NETWORK_$MOTHER_NODE (0x00E24C0C =
                                     *       A5+0x310), "move.l (0x310,A5),
                                     *       (-0x1de,A0)" at 0x00E112C8.
                                     *       netmain: "<node> pages from <node>." */

    /* ---- 0x08: RING_$DATA[0] (0x00E261E0), 0x3C bytes -------------- *
     * "movea.l #0xe261e0,A1 / lea (-0x1da,A0),A4 / moveq #0xe,D1 /
     *  move.l (A1)+,(A4)+ / dbf" at 0x00E11266..0x00E11274: fifteen longwords.
     * Field names, offsets and per-counter evidence: ring_$stats_t, ring/ring.h. */
    struct {
        uint16_t    _reserved0;     /* 0x08 (stats+0x00) */
        uint32_t    xmit_call;      /* 0x0A (stats+0x02) */
        uint32_t    xmitcnt;        /* 0x0E (stats+0x06) */
        uint16_t    xmit_nack;      /* 0x12 (stats+0x0A) */
        uint16_t    xmit_wack;      /* 0x14 (stats+0x0C) */
        uint16_t    xmit_orun;      /* 0x16 (stats+0x0E) */
        uint16_t    xmit_apar;      /* 0x18 (stats+0x10) */
        uint16_t    xmit_bus;       /* 0x1A (stats+0x12) */
        uint16_t    xmit_nortn;     /* 0x1C (stats+0x14) */
        uint16_t    xmit_modem;     /* 0x1E (stats+0x16) */
        uint16_t    xmit_error;     /* 0x20 (stats+0x18) */
        uint16_t    xmit_tim;       /* 0x22 (stats+0x1A) */
        uint32_t    rcvcnt;         /* 0x24 (stats+0x1C) */
        uint16_t    rcveor;         /* 0x28 (stats+0x20) */
        uint16_t    rcvcrc;         /* 0x2A (stats+0x22) */
        uint16_t    rcvtim;         /* 0x2C (stats+0x24) */
        uint16_t    rcvbus;         /* 0x2E (stats+0x26) */
        uint16_t    rcvmodem;       /* 0x30 (stats+0x28) */
        uint16_t    rcvpkt;         /* 0x32 (stats+0x2A) */
        uint16_t    rcvovr;         /* 0x34 (stats+0x2C) */
        uint16_t    rcvapar;        /* 0x36 (stats+0x2E) */
        uint16_t    rcvxerr;        /* 0x38 (stats+0x30) */
        uint16_t    rcvhcsum;       /* 0x3A (stats+0x32) */
        int8_t      last_success;   /* 0x3C (stats+0x34) */
        int8_t      _reserved2;     /* 0x3D (stats+0x35) */
        int8_t      congestion_flag;/* 0x3E (stats+0x36) */
        int8_t      _reserved3;     /* 0x3F (stats+0x37) */
        int8_t      biphase_flag;   /* 0x40 (stats+0x38) */
        int8_t      _reserved4;     /* 0x41 (stats+0x39) */
        int8_t      retry_pending;  /* 0x42 (stats+0x3A) */
        int8_t      _reserved5;     /* 0x43 (stats+0x3B) */
    } __attribute__((packed)) stats;

    /* ---- 0x44: NETWORK_$FAILURE_REC (0x00E24BF4 = A5+0x2F8) --------- *
     * "lea (0x2f8,A5),A1 / lea (-0x19e,A0),A4" + four "move.l (A1)+,(A4)+"
     * at 0x00E11256..0x00E11264.  netmain's hardware-failure display reads
     * "v<n>  Failure type = <lh>", "Status bits: broken / not broken",
     * "forced last time / did not force last time", "delay in / delay out",
     * "forcing now / not forcing now" and "reported by <node> at <time>". */
    network_$failure_rec_t  failure_rec;    /* 0x44..0x53 */

    /* ---- 0x54: RING_$SWDIAG_DATA (0x00E261C2), 0x1E bytes ----------- *
     * "movea.l #0xe261c2,A1 / lea (-0x18e,A0),A4 / moveq #0x6 /
     *  move.l (A1)+,(A4)+ / dbf / move.w (A1)+,(A4)+" at
     * 0x00E11278..0x00E1128A: seven longwords plus one word = 30 bytes, i.e.
     * 0x00E261C2..0x00E261DF, which runs up to but not into RING_$DATA[0] at
     * 0x00E261E0.  ring/ring.h declares ring_$swdiag_t at that same 0x1E
     * (bead source-twut).
     *
     * Two longwords of that copy are then overwritten in place from the
     * standalone globals that hold the live values, so the bytes the block
     * copy put at +0x56..+0x59 and +0x6E..+0x71 are dead. */
    struct {
        uint16_t    _unknown_54;    /* 0x54 (swdiag+0x00) */
        uint32_t    rcvcnt;         /* 0x56: RING_$SWDIAG_RCVCNT (0x00E261B4),
                                     *       "move.l (0x00e261b4).l,(-0x18c,A0)"
                                     *       at 0x00E1128C - overwrites the
                                     *       block copy of swdiag+0x02..0x05 */
        uint16_t    rcveor;         /* 0x5A (swdiag+0x06) */
        uint16_t    rcvcrc;         /* 0x5C (swdiag+0x08) */
        uint16_t    rcvtim;         /* 0x5E (swdiag+0x0A) */
        uint16_t    rcvbus;         /* 0x60 (swdiag+0x0C) */
        uint16_t    rcvmodem;       /* 0x62 (swdiag+0x0E) */
        uint16_t    rcvpkt;         /* 0x64 (swdiag+0x10) */
        uint16_t    rcvovr;         /* 0x66 (swdiag+0x12) */
        uint16_t    rcvapar;        /* 0x68 (swdiag+0x14) */
        uint16_t    rcvxerr;        /* 0x6A (swdiag+0x16) */
        uint16_t    rcvhcsum;       /* 0x6C (swdiag+0x18): the swdiag mirror
                                     *       sits a uniform 0x1A below its
                                     *       ring_$stats_t counter and
                                     *       rcvhcsum is stats+0x32, so this is
                                     *       its slot; netmain's software-
                                     *       diagnostic display prints
                                     *       "rcvxerr <n>  rcvhcsum <n>" as its
                                     *       own line.  The receive path never
                                     *       bumps this mirror. */
        uint32_t    nodeid;         /* 0x6E: RING_$SWDIAG_NODEID (0x00E261AC),
                                     *       "move.l (0x00e261ac).l,(-0x174,A0)"
                                     *       at 0x00E11294 - overwrites the
                                     *       block copy of swdiag+0x1A..0x1D */
    } __attribute__((packed)) swdiag;

    /* ---- 0x72: the four standalone biphase/ESB words ---------------- *
     * netmain prints them in exactly this order ("xmit bph", "rcv bph",
     * "xmit esb", then "rcv esb" on the software-diagnostic line), and the two
     * receive ones are already pinned to their globals by ring_$validate_receive
     * (0x00E75F7C), which fixes the other two. */
    uint16_t    xmit_biphase;       /* 0x72: RING_$XMIT_BIPHASE (0x00E261BC),
                                     *       0x00E1129C */
    uint16_t    rcv_biphase;        /* 0x74: RING_$RCV_BIPHASE (0x00E261B8),
                                     *       0x00E112A4 */
    uint16_t    xmit_esb;           /* 0x76: RING_$XMIT_ESB (0x00E261BE),
                                     *       0x00E112AC */
    uint16_t    rcv_esb;            /* 0x78: RING_$RCV_ESB (0x00E261BA),
                                     *       0x00E112B4 */
} __attribute__((packed)) ring_info_t;

_Static_assert(sizeof(ring_info_t) == 122, "sizeof ring_info_t");
_Static_assert(offsetof(ring_info_t, diskless)             == 0x02, "ring_info_t.diskless");
_Static_assert(offsetof(ring_info_t, mother_node)          == 0x04, "ring_info_t.mother_node");
_Static_assert(offsetof(ring_info_t, stats)                == 0x08, "ring_info_t.stats");
_Static_assert(offsetof(ring_info_t, stats.rcvcnt)         == 0x24, "ring_info_t.stats.rcvcnt");
_Static_assert(offsetof(ring_info_t, stats.rcvhcsum)       == 0x3A, "ring_info_t.stats.rcvhcsum");
_Static_assert(offsetof(ring_info_t, failure_rec)          == 0x44, "ring_info_t.failure_rec");
_Static_assert(offsetof(ring_info_t, swdiag)               == 0x54, "ring_info_t.swdiag");
_Static_assert(offsetof(ring_info_t, swdiag.rcvcnt)        == 0x56, "ring_info_t.swdiag.rcvcnt");
_Static_assert(offsetof(ring_info_t, swdiag.rcvhcsum)      == 0x6C, "ring_info_t.swdiag.rcvhcsum");
_Static_assert(offsetof(ring_info_t, swdiag.nodeid)        == 0x6E, "ring_info_t.swdiag.nodeid");
_Static_assert(offsetof(ring_info_t, xmit_biphase)         == 0x72, "ring_info_t.xmit_biphase");
_Static_assert(offsetof(ring_info_t, rcv_esb)              == 0x78, "ring_info_t.rcv_esb");

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
/* NODE_$ME (0xE245A4): defined in uid/uid_data.c, declared in uid/uid.h */
#include "uid/uid.h"

/*
 * network_$fetch_diskless_info - Fetch info from network for diskless boot
 *
 * Queries ASKNODE_$INTERNET_INFO for node-specific data and processes
 * the result based on the command type:
 *
 *   cmd=2  (BOOT_TIME):  Update TIME_$CLOCKH from remote node's clock
 *   cmd=8  (TIMEZONE):   Update CAL_$TIMEZONE (timezone record)
 *   cmd=0x37 (ROUTING):  Update routing table if route port changed
 *
 * On error for cmd != 0x37: calls CRASH_SYSTEM (fatal).
 * cmd=0x37 tolerates errors gracefully.
 *
 * @param cmd   Command type (2=time, 8=timezone, 0x37=routing)
 * @param node  Network node address (typically NETWORK_$MOTHER_NODE)
 *
 * Original address: 0x00E3366C
 */
void network_$fetch_diskless_info(int16_t cmd, uint32_t node);

/*
 * NETWORK_$INIT - Initialize the network subsystem
 *
 * Original address: 0x00E2F684
 * TODO(source-sdx1): NOT EMITTED.  358 bytes at 0x00E2F684..0x00E2F7F1;
 * only the prototype exists, so OS_$INIT's call does not link.  Tracked in
 * the network link inventory as source-sdx1.
 */
void NETWORK_$INIT(void);

/*
 * NETWORK_$LOAD - Late network initialization (after PROC2_$INIT)
 *
 * Original address: 0x00E2F7F2
 * TODO(source-sdx1): NOT EMITTED.  86 bytes at 0x00E2F7F2..0x00E2F847;
 * only the prototype exists, so OS_$INIT's call does not link.  Tracked in
 * the network link inventory as source-sdx1.
 */
void NETWORK_$LOAD(void);

/*
 * NETWORK_$DISMISS_REQUEST_SERVERS - Dismiss the network request server
 * processes during OS_$SHUTDOWN.
 *
 * Original address: 0x00E71E78
 * TODO(source-sdx1): NOT EMITTED.  74 bytes at 0x00E71E78..0x00E71EC1;
 * only the prototype exists, so OS_$SHUTDOWN's call does not link.  Tracked
 * in the network link inventory as source-sdx1.
 */
void NETWORK_$DISMISS_REQUEST_SERVERS(void);

#endif /* NETWORK_H */
