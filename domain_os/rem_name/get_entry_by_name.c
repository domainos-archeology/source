/*
 * rem_name/get_entry_by_name.c - REM_NAME_$GET_ENTRY_BY_NAME (0x00E4A588, 264 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4A590).
 *
 * Ask the name server on (net, node) for the entry called `name` in
 * directory `dir_uid`.  Names longer than 32 bytes are refused locally.
 *
 * Frame (A6+): 0x08 net, 0x0C node, 0x10 dir_uid -> A3, 0x14 name -> A1,
 * 0x18 name_len (word) -> D2w, 0x1A entry_ret -> A2, 0x1E status_ret -> D3.
 * Locals (A6-): -0x1C8 request (0x58 bytes, D4), -0x170 reply (0x16A bytes),
 * -0x1D0 reply length word.
 */

#include "rem_name/rem_name_internal.h"

/*
 * Request laid out at A6-0x1C8: opcode, directory UID, a word of 1, then
 * (after 0x24 bytes the routine never writes) the name length at 0x32 and
 * the name from 0x34.  Only 0x34 + name_len bytes are sent (0x00E4A606).
 */
typedef struct __attribute__((packed, aligned(2))) rem_name_$by_name_req_t {
    uint32_t    opcode;         /* 0x00: 0x00010001 */
    uid_t       dir_uid;        /* 0x04 */
    uint16_t    one;            /* 0x0C: 1 */
    uint8_t     unset[0x24];    /* 0x0E: never stored */
    uint16_t    name_len;       /* 0x32 */
    char        name[32];       /* 0x34 */
    uint8_t     pad[4];         /* 0x54: frame slack up to the reply buffer */
} rem_name_$by_name_req_t;

_Static_assert(__builtin_offsetof(rem_name_$by_name_req_t, name_len) == 0x32, "by_name_req.name_len");
_Static_assert(__builtin_offsetof(rem_name_$by_name_req_t, name)     == 0x34, "by_name_req.name");
_Static_assert(sizeof(rem_name_$by_name_req_t) == 0x58, "sizeof rem_name_$by_name_req_t");

void REM_NAME_$GET_ENTRY_BY_NAME(uint32_t net, uint32_t node, uid_t *dir_uid,
                                  char *name, uint16_t name_len,
                                  void *entry_ret, status_$t *status_ret)
{
    rem_name_$by_name_req_t  request;               /* A6-0x1C8 */
    rem_name_$entry_reply_t  reply;                 /* A6-0x170 */
    int16_t                  reply_len;             /* A6-0x1D0 */
    rem_name_$dir_entry_t   *entry = (rem_name_$dir_entry_t *)entry_ret;
    int16_t                  count;
    int16_t                  i;

    /* 0x00E4A5A6-0x00E4A5B4: `cmpi.w #0x20,D2w / bls` */
    if (name_len > 0x20) {
        *status_ret = status_$naming_invalid_pathname;
        return;
    }

    /* 0x00E4A5B8-0x00E4A5D8 */
    request.opcode   = REM_NAME_OP_GET_ENTRY_BY_NAME;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low  = dir_uid->low;
    request.one      = 1;
    request.name_len = name_len;

    /* 0x00E4A5DC-0x00E4A5F0: name[1..len] -> request+0x33+i, dbf len-1
     * (skipped entirely for an empty name). */
    count = (int16_t)(name_len - 1);
    if (count >= 0) {
        i = 1;
        do {
            request.name[i - 1] = name[i - 1];
            i = (int16_t)(i + 1);
            count = (int16_t)(count - 1);
        } while (count != -1);
    }

    /* 0x00E4A5F4-0x00E4A620: send 0x34 + len bytes, opcode word 2 (the
     * `pea (0x2).w` longword is flags 0 / opcode 2 in the callee's frame),
     * reply buffer 0x16A bytes. */
    if (rem_name_$send_request(net, node, &request, (int16_t)(0x34 + name_len),
                               0, 2, &reply, REM_NAME_REPLY_SIZE,
                               &reply_len, status_ret) >= 0) {
        return;
    }

    /* 0x00E4A622-0x00E4A636: name length and 32 name bytes, unconditionally */
    entry->name_len = reply.name_len;
    for (i = 0; i < 32; i++) {
        entry->name[i] = reply.name[i];
    }

    /* 0x00E4A63A-0x00E4A64A */
    if (reply.type == ENTRY_TYPE_NORMAL) {
        /* 0x00E4A64C-0x00E4A662: 12 bytes = uid + extra */
        entry->type = 1;
        for (i = 0; i < 12; i++) {
            ((uint8_t *)&entry->uid)[i] = reply.data[i];
        }
    } else if (reply.type == ENTRY_TYPE_LINK) {
        /* 0x00E4A664-0x00E4A67A */
        entry->type = ENTRY_TYPE_LINK_ALT;
        entry->uid.high = UID_$NIL.high;
        entry->uid.low  = UID_$NIL.low;
        entry->extra = 0;
    } else {
        /* 0x00E4A67C-0x00E4A684 */
        *status_ret = status_$naming_name_not_found;
        entry->type = 0;
    }
}
