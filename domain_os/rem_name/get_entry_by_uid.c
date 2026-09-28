/*
 * rem_name/get_entry_by_uid.c - REM_NAME_$GET_ENTRY_BY_UID (0x00E4A8CC, 184 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4A8D4).
 *
 * Ask the name server on (net, node) for the entry whose UID is
 * `target_uid` in directory `dir_uid`.
 *
 * Frame (A6+): 0x08 net, 0x0C node, 0x10 dir_uid, 0x14 target_uid,
 * 0x18 entry_ret -> A3, 0x1C status_ret -> D2.
 * Locals (A6-): -0x1B0 request (0x40 bytes, A2), -0x170 reply (0x16A),
 * -0x1B6 reply length word.
 */

#include "rem_name/rem_name_internal.h"

/* Request at A6-0x1B0; 0x3A bytes are sent (0x00E4A91E). */
typedef struct __attribute__((packed, aligned(2))) rem_name_$by_uid_req_t {
    uint32_t    opcode;         /* 0x00: 0x0001001B */
    uid_t       dir_uid;        /* 0x04 */
    uint16_t    one;            /* 0x0C: 1 */
    uint8_t     unset[0x24];    /* 0x0E: never stored */
    uid_t       target_uid;     /* 0x32 */
    uint8_t     pad[6];         /* 0x3A: frame slack up to the reply buffer */
} rem_name_$by_uid_req_t;

_Static_assert(__builtin_offsetof(rem_name_$by_uid_req_t, target_uid) == 0x32, "by_uid_req.target_uid");
_Static_assert(sizeof(rem_name_$by_uid_req_t) == 0x40, "sizeof rem_name_$by_uid_req_t");

void REM_NAME_$GET_ENTRY_BY_UID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                 uid_t *target_uid, void *entry_ret,
                                 status_$t *status_ret)
{
    rem_name_$by_uid_req_t   request;               /* A6-0x1B0 */
    rem_name_$entry_reply_t  reply;                 /* A6-0x170 */
    int16_t                  reply_len;             /* A6-0x1B6 */
    rem_name_$dir_entry_t   *entry = (rem_name_$dir_entry_t *)entry_ret;
    int16_t                  i;

    /* 0x00E4A8E6-0x00E4A908 */
    request.opcode   = REM_NAME_OP_GET_ENTRY_BY_UID;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low  = dir_uid->low;
    request.one      = 1;
    request.target_uid.high = target_uid->high;
    request.target_uid.low  = target_uid->low;

    /* 0x00E4A90C-0x00E4A936: 0x3A bytes, opcode word 0x1C (`pea (0x1c).w`),
     * reply buffer 0x16A bytes. */
    if (rem_name_$send_request(net, node, &request, 0x3A, 0, 0x1C,
                               &reply, REM_NAME_REPLY_SIZE,
                               &reply_len, status_ret) >= 0) {
        return;
    }

    /* 0x00E4A938-0x00E4A94A */
    entry->name_len = reply.name_len;
    for (i = 0; i < 32; i++) {
        entry->name[i] = reply.name[i];
    }

    /* 0x00E4A94E-0x00E4A96E */
    if (reply.type == ENTRY_TYPE_NORMAL) {
        entry->type = 1;
        for (i = 0; i < 12; i++) {
            ((uint8_t *)&entry->uid)[i] = reply.data[i];
        }
    } else {
        /* 0x00E4A970-0x00E4A978 */
        *status_ret = status_$naming_name_not_found;
        entry->type = 0;
    }
}
