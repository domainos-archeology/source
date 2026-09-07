/*
 * REM_NAME - Internal Header
 *
 * Internal types, data structures, and helper functions for the REM_NAME
 * module (SAU2 map: I 0xE4A408 size 0xB20, D 0xE7DBB8 size 0x40).
 * This file should only be included by .c files within the rem_name/
 * directory.
 *
 * Functions to handle distributed naming operations across Apollo network
 * nodes.  These functions communicate with remote naming servers to resolve
 * names, get directory information, and manage network-wide naming.
 *
 * Original addresses: 0x00E4A408 - 0x00E4AF24
 */

#ifndef REM_NAME_INTERNAL_H
#define REM_NAME_INTERNAL_H

#include "rem_name/rem_name.h"

#include "misc/string.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "os/os.h"
#include "pkt/pkt.h"
#include "sock/sock.h"
#include "time/time.h"
#include "uid/uid.h"

/* TIME_$CLOCKH, NODE_$ME, UID_$NIL, OS_$DATA_COPY and PKT_$SAR_INTERNET are
 * declared by the headers pulled in above.  REM_NAME_SERVER_LOCAL is declared
 * in name/name.h, reached through rem_name/rem_name.h. */

/*
 * REM_NAME data area - complete structure at 0xE7DBB8 (D segment size 0x40).
 * Defined in rem_name/rem_name_data.c.
 */
typedef struct rem_name_data_t {
    uint16_t config[15];             /* +0x00: Config copied to request packets */
    uint16_t reserved1;              /* +0x1E: Reserved */
    uint32_t server_timeout;         /* +0x20: Timeout for server contact */
    uint32_t reserved2;              /* +0x24: Reserved */
    uint32_t time_heard_from_server; /* +0x28: TIME_$CLOCKH when last heard
                                      *        (map: REM_NAME_$TIME_HEARD_FROM_SERVER) */
    status_$t last_status;           /* +0x2C: Last status code
                                      *        (map: REM_NAME_$LAST_STATUS) */
    uint32_t curr_node;              /* +0x30: Current name server node
                                      *        (map: REM_NAME_$CURR_NODE) */
    uint32_t curr_net;               /* +0x34: Current name server network
                                      *        (map: REM_NAME_$CURR_NET) */
    /*
     * +0x38: the `timeout` argument rem_name_$send_request hands
     * PKT_$SAR_INTERNET - the fifth of its seventeen arguments
     * (`move.w (0x38,A5),-(SP)` at 0x00E4A524, thirteenth of the seventeen
     * pushes).  PKT_$SAR_INTERNET adds it to the word the send returned and
     * turns the sum into the response deadline (`add.w (0x16,A6),D0w` at
     * 0x00E71FBA, then `add.l (0x00e2b0d4).l,D0`), so it is an extra wait in
     * clock ticks, not a sequence number.  The image initialises it to 0x0010
     * and nothing else in the module ever touches A5+0x38.  (source-qg0q.)
     * The SAU2 map names this cell REM_NAME_$SERVICE_DELAY.
     */
    uint16_t service_delay;  /* 0xE7DBF0: map symbol REM_NAME_$SERVICE_DELAY; PKT_$SAR_INTERNET timeout arg (0x00E4A524) */
    uint16_t retry_count;            /* +0x3A: Server locate retry counter */
    int8_t   heard_from_server;      /* +0x3C: True if contacted server
                                      *        (map: REM_NAME_$HEARD_FROM_SERVER) */
} rem_name_data_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(rem_name_data_t, config) == 0x00, "rem_name_data_t.config");
_Static_assert(__builtin_offsetof(rem_name_data_t, reserved1) == 0x1E, "rem_name_data_t.reserved1");
_Static_assert(__builtin_offsetof(rem_name_data_t, server_timeout) == 0x20, "rem_name_data_t.server_timeout");
_Static_assert(__builtin_offsetof(rem_name_data_t, reserved2) == 0x24, "rem_name_data_t.reserved2");
_Static_assert(__builtin_offsetof(rem_name_data_t, time_heard_from_server) == 0x28, "rem_name_data_t.time_heard_from_server");
_Static_assert(__builtin_offsetof(rem_name_data_t, last_status) == 0x2C, "rem_name_data_t.last_status");
_Static_assert(__builtin_offsetof(rem_name_data_t, curr_node) == 0x30, "rem_name_data_t.curr_node");
_Static_assert(__builtin_offsetof(rem_name_data_t, curr_net) == 0x34, "rem_name_data_t.curr_net");
_Static_assert(__builtin_offsetof(rem_name_data_t, service_delay) == 0x38, "rem_name_data_t.service_delay");
_Static_assert(__builtin_offsetof(rem_name_data_t, retry_count) == 0x3A, "rem_name_data_t.retry_count");
_Static_assert(__builtin_offsetof(rem_name_data_t, heard_from_server) == 0x3C, "rem_name_data_t.heard_from_server");
/*
 * The SAU2 map gives the whole D segment as 0x40 bytes ("D E7DBB8 REM_NAME
 * size = 40").  The last field the code touches is the boolean at +0x3C, so
 * the record itself ends at 0x3D and the map's 0x40 is the segment rounded up
 * to a longword; the record must therefore fit in, not fill, 0x40 bytes.
 */
_Static_assert(sizeof(rem_name_data_t) <= 0x40, "rem_name_data_t fits the SAU2 map D 0xE7DBB8 size = 40");

extern rem_name_data_t rem_name_$data;  /* 0xE7DBB8 */

/*
 * Socket used by the remote naming service.
 *
 * REM_NAME_SERVER_LOCAL (0x00E4A408) does
 *     movea.l (0x00e28dd8).l,A0 ; move.w (0x16,A0),D0w ; btst.l #0xd,D0
 * 0xE28DD8 is not an eventcount of its own: it is slot 10 of the SOCK socket
 * pointer table (sock_table_base + 0x18A4 + 9*4, that is
 * SOCK_$EVENT_COUNTERS[REM_NAME_$SOCK - 1]), and the word at +0x16 of the
 * socket descriptor it points at is sock_$sock_t.flags.  Bit 13 of that word
 * means "the name server runs on this node".  The descriptor declaration
 * lives in sock/sock.h; only the socket number belongs to REM_NAME.
 */
#define REM_NAME_$SOCK          10      /* well-known naming-service socket */
#define SOCK_FLAG_SERVER_LOCAL  0x2000  /* sock_$sock_t.flags bit 13 */

/* Status codes for remote naming are in name/name.h */

/*
 * Request opcodes for remote naming operations
 */
#define REM_NAME_OP_GET_ENTRY_BY_NAME   0x10001
#define REM_NAME_OP_READ_DIR            0x1000b
#define REM_NAME_OP_READ_REP            0x1000d
#define REM_NAME_OP_GET_ENTRY_BY_NODE   0x10017
#define REM_NAME_OP_GET_INFO            0x10019
#define REM_NAME_OP_GET_ENTRY_BY_UID    0x1001b
#define REM_NAME_OP_LOCATE_SERVER       0x1001d

/*
 * Response type codes
 */
#define ENTRY_TYPE_NORMAL   1
#define ENTRY_TYPE_LINK     2
#define ENTRY_TYPE_LINK_ALT 3

/*
 * Entry structure size for directory reads
 */
#define DIR_ENTRY_SIZE      0x30
#define REP_ENTRY_SIZE      0x12

/*
 * rem_name_$send_request (0x00E4A4C8) - the module's RPC helper.  It has no
 * symbol of its own in the SAU2 map; the lowercase name is ours.  Module
 * local: only rem_name/ calls it.
 */
boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                               int16_t req_size, int16_t flags, int16_t opcode,
                               void *response, int16_t resp_size,
                               int16_t *resp_len_ret, status_$t *status_ret);

/*
 * LOCATE_SERVER (0x00E4A420) - the module-local server-location helper, kept
 * under the name the SAU2 map gives it.  Distinct from the exported
 * REM_NAME_$LOCATE_SERVER (0x00E4A722), which it calls.
 */
void LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret);

#endif /* REM_NAME_INTERNAL_H */
