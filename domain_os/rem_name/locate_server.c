/*
 * rem_name/locate_server.c - REM_NAME_$LOCATE_SERVER (0x00E4A722, 222 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4A72A).
 *
 * Find a name server: when REM_NAME_SERVER_LOCAL says one runs here, ask
 * NODE_$ME directly and accept a reply of EXACTLY 0x28 bytes; otherwise (or
 * when that failed) broadcast to node 0xFFFFFF with flag 0x80 and accept a
 * reply of at least 0x28 bytes.  The located node (20 bits of the reply's
 * +0x1E longword) becomes curr_node with curr_net = 0, and last_status is
 * refreshed on every path except a failed broadcast.
 *
 * Frame (A6+): 0x08 node_ret -> D2, 0x0C net_ret -> D3, 0x10 status_ret -> A3.
 * Locals (A6-): -0x1A8 request (0x38 bytes, A2), -0x170 reply (0x16A),
 * -0x1AE reply length word, -0x1B6 send result byte.
 */

#include "rem_name/rem_name_internal.h"

/* Request at A6-0x1A8: opcode and the word 1 at 0x0C; 0x32 bytes are sent
 * (0x00E4A768 / 0x00E4A7A6) and 0x04..0x0B are never stored. */
typedef struct __attribute__((packed, aligned(2))) rem_name_$locate_req_t {
    uint32_t    opcode;         /* 0x00: 0x0001001D */
    uint8_t     unset_04[8];    /* 0x04: never stored */
    uint16_t    one;            /* 0x0C: 1 */
    uint8_t     unset_0e[0x2A]; /* 0x0E: never stored */
} rem_name_$locate_req_t;

_Static_assert(__builtin_offsetof(rem_name_$locate_req_t, one) == 0x0C, "locate_req.one");
_Static_assert(sizeof(rem_name_$locate_req_t) == 0x38, "sizeof rem_name_$locate_req_t");

/* 0x00E4A7E4 `and.l (-0x152,A6),D1`: reply + 0x1E, masked to 20 bits. */
#define REM_NAME_LOCATE_REPLY_NODE_OFF  0x1E
#define REM_NAME_LOCATE_BROADCAST_NODE  0xFFFFFFu
#define REM_NAME_LOCATE_BROADCAST_FLAG  0x80

void REM_NAME_$LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    rem_name_$locate_req_t   request;               /* A6-0x1A8 */
    rem_name_$entry_reply_t  reply;                 /* A6-0x170 */
    int16_t                  reply_len;             /* A6-0x1AE */
    boolean                  sent;                  /* A6-0x1B6 */
    uint32_t                 located;               /* D1 */
    uint32_t                 raw;

    /* 0x00E4A740-0x00E4A748 */
    request.opcode = REM_NAME_OP_LOCATE_SERVER;
    request.one    = 1;

    /* 0x00E4A74E-0x00E4A754 */
    if (REM_NAME_SERVER_LOCAL() < 0) {
        /* 0x00E4A756-0x00E4A790: (net 0, NODE_$ME, req, 0x32, flags 0,
         * opcode 0x1E, reply, 0x16A, &len, status) - `pea (0x1e).w` */
        sent = rem_name_$send_request(0, NODE_$ME, &request, 0x32, 0, 0x1E,
                                      &reply, REM_NAME_REPLY_SIZE,
                                      &reply_len, status_ret);
        if (sent < 0 && reply_len == 0x28) {        /* cmpi.w #0x28 / beq */
            goto found;
        }
    }

    /* 0x00E4A792-0x00E4A7C8: broadcast; `move.l #0x80001e` is flags 0x80,
     * opcode 0x1E in the callee's frame. */
    sent = rem_name_$send_request(0, REM_NAME_LOCATE_BROADCAST_NODE, &request,
                                  0x32, REM_NAME_LOCATE_BROADCAST_FLAG, 0x1E,
                                  &reply, REM_NAME_REPLY_SIZE,
                                  &reply_len, status_ret);
    if (sent >= 0) {
        return;                                     /* 0x00E4A7F6: last_status untouched */
    }

    /* 0x00E4A7CA-0x00E4A7D6: `cmpi.w #0x28 / bge` (signed) */
    if (reply_len < 0x28) {
        *status_ret = status_$naming_helper_sent_packets_with_errors;
        goto record_status;
    }

found:
    /* 0x00E4A7D8-0x00E4A7EE */
    *net_ret = 0;
    raw = (uint32_t)((const uint8_t *)&reply)[REM_NAME_LOCATE_REPLY_NODE_OFF]     << 24 |
          (uint32_t)((const uint8_t *)&reply)[REM_NAME_LOCATE_REPLY_NODE_OFF + 1] << 16 |
          (uint32_t)((const uint8_t *)&reply)[REM_NAME_LOCATE_REPLY_NODE_OFF + 2] << 8  |
          (uint32_t)((const uint8_t *)&reply)[REM_NAME_LOCATE_REPLY_NODE_OFF + 3];
    located = raw & 0xFFFFFu;
    *node_ret = located;
    rem_name_$data.curr_net  = 0;
    rem_name_$data.curr_node = located;

record_status:
    /* 0x00E4A7F2 */
    rem_name_$data.last_status = *status_ret;
}
