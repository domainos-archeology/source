/*
 * rem_name/get_info.c - REM_NAME_$GET_INFO (0x00E4A690, 146 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4A698).
 *
 * Ask the name server on (net, node) about object `uid` and copy the 22
 * bytes it answers with into *info_ret - but only when the reply is
 * EXACTLY 0x28 bytes long (`cmpi.w #0x28,D0w / bne` at 0x00E4A6F6); any
 * other accepted length is reported as "helper sent packets with errors".
 *
 * Frame (A6+): 0x08 net, 0x0C node, 0x10 uid, 0x14 info_ret,
 * 0x18 status_ret -> A3.
 * Locals (A6-): -0x1A8 request (0x38 bytes, A2), -0x170 reply (0x16A),
 * -0x1AE reply length word, -0x1B6 send result byte.
 */

#include "rem_name/rem_name_internal.h"

/* Request at A6-0x1A8; 0x32 bytes are sent (0x00E4A6D2). */
typedef struct __attribute__((packed, aligned(2))) rem_name_$get_info_req_t {
    uint32_t    opcode;         /* 0x00: 0x00010019 */
    uid_t       uid;            /* 0x04 */
    uint16_t    one;            /* 0x0C: 1 */
    uint8_t     unset[0x2A];    /* 0x0E: never stored */
} rem_name_$get_info_req_t;

_Static_assert(sizeof(rem_name_$get_info_req_t) == 0x38, "sizeof rem_name_$get_info_req_t");

/* What 0x00E4A6FC-0x00E4A70E copies: five longwords and a word from the
 * reply's +0x12. */
typedef struct __attribute__((packed, aligned(2))) rem_name_$info_t {
    uint32_t    words[5];       /* 0x00 */
    uint16_t    tail;           /* 0x14 */
} rem_name_$info_t;

_Static_assert(sizeof(rem_name_$info_t) == 22, "sizeof rem_name_$info_t");

void REM_NAME_$GET_INFO(uint32_t net, uint32_t node, uid_t *uid,
                        void *info_ret, status_$t *status_ret)
{
    rem_name_$get_info_req_t  request;              /* A6-0x1A8 */
    rem_name_$entry_reply_t   reply;                /* A6-0x170 */
    int16_t                   reply_len;            /* A6-0x1AE */
    boolean                   sent;                 /* A6-0x1B6 */
    const rem_name_$info_t   *src;
    rem_name_$info_t         *dst = (rem_name_$info_t *)info_ret;
    int16_t                   i;

    /* 0x00E4A6A6-0x00E4A6BA */
    request.opcode   = REM_NAME_OP_GET_INFO;
    request.uid.high = uid->high;
    request.uid.low  = uid->low;
    request.one      = 1;

    /* 0x00E4A6C0-0x00E4A6E8: 0x32 bytes, opcode word 0x1A (`pea (0x1a).w`) */
    sent = rem_name_$send_request(net, node, &request, 0x32, 0, 0x1A,
                                  &reply, REM_NAME_REPLY_SIZE,
                                  &reply_len, status_ret);

    /* 0x00E4A6EC-0x00E4A6F4: nothing more when the send failed */
    if (sent >= 0) {
        return;
    }

    /* 0x00E4A6F6-0x00E4A716 */
    if (reply_len == 0x28) {
        src = (const rem_name_$info_t *)&reply.type;         /* +0x12 */
        for (i = 0; i < 5; i++) {
            dst->words[i] = src->words[i];
        }
        dst->tail = src->tail;
    } else {
        *status_ret = status_$naming_helper_sent_packets_with_errors;
    }
}
