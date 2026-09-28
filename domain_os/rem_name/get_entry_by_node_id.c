/*
 * rem_name/get_entry_by_node_id.c - REM_NAME_$GET_ENTRY_BY_NODE_ID (0x00E4A800, 204 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4A808).
 *
 * Ask the name server on (net, node) for the network entry of node id
 * `target_node` in directory `dir_uid`.
 *
 * Frame (A6+): 0x08 net, 0x0C node, 0x10 dir_uid, 0x14 target_node (long),
 * 0x18 entry_ret -> A3, 0x1C status_ret -> D2.
 * Locals (A6-): -0x1A8 request (0x38 bytes, A2), -0x170 reply (0x16A),
 * -0x1AE reply length word.
 */

#include "rem_name/rem_name_internal.h"

/*
 * Request at A6-0x1A8.  The node id is built with two overlapping longword
 * read-modify-writes on uninitialised frame bytes:
 *   0x00E4A834  andi.l #0xff,(0x32,A0)        keep byte 0x35
 *   0x00E4A83C  ori.l  #0x8001e00,(0x32,A0)   bytes 0x32..0x35 |= 08 00 1E 00
 *   0x00E4A844  andi.l #-0x1000000,(0x34,A0)  keep byte 0x34 (= 0x1E)
 *   0x00E4A850  or.l   D0,(0x34,A0)           |= target_node
 * Every byte the first RMW preserved is cleared by the second, so the
 * result is exactly word 0x0800 at 0x32 and 0x1E000000 | target_node at
 * 0x34, which is how it is written here.
 */
typedef struct __attribute__((packed, aligned(2))) rem_name_$by_node_req_t {
    uint32_t    opcode;         /* 0x00: 0x00010017 */
    uid_t       dir_uid;        /* 0x04 */
    uint16_t    one;            /* 0x0C: 1 */
    uint8_t     unset[0x24];    /* 0x0E: never stored */
    uint16_t    node_tag;       /* 0x32: 0x0800 */
    uint32_t    node_id;        /* 0x34: 0x1E000000 | target_node */
} rem_name_$by_node_req_t;

_Static_assert(__builtin_offsetof(rem_name_$by_node_req_t, node_tag) == 0x32, "by_node_req.node_tag");
_Static_assert(__builtin_offsetof(rem_name_$by_node_req_t, node_id)  == 0x34, "by_node_req.node_id");
_Static_assert(sizeof(rem_name_$by_node_req_t) == 0x38, "sizeof rem_name_$by_node_req_t");

void REM_NAME_$GET_ENTRY_BY_NODE_ID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                     uint32_t target_node, void *entry_ret,
                                     status_$t *status_ret)
{
    rem_name_$by_node_req_t  request;               /* A6-0x1A8 */
    rem_name_$entry_reply_t  reply;                 /* A6-0x170 */
    int16_t                  reply_len;             /* A6-0x1AE */
    rem_name_$dir_entry_t   *entry = (rem_name_$dir_entry_t *)entry_ret;
    int16_t                  i;

    /* 0x00E4A81A-0x00E4A850 */
    request.opcode   = REM_NAME_OP_GET_ENTRY_BY_NODE;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low  = dir_uid->low;
    request.one      = 1;
    request.node_tag = 0x0800;
    request.node_id  = 0x1E000000u | target_node;

    /* 0x00E4A854-0x00E4A87E: 0x38 bytes, opcode word 0x18 (`pea (0x18).w`),
     * reply buffer 0x16A bytes. */
    if (rem_name_$send_request(net, node, &request, 0x38, 0, 0x18,
                               &reply, REM_NAME_REPLY_SIZE,
                               &reply_len, status_ret) >= 0) {
        return;
    }

    /* 0x00E4A880-0x00E4A892 */
    entry->name_len = reply.name_len;
    for (i = 0; i < 32; i++) {
        entry->name[i] = reply.name[i];
    }

    /* 0x00E4A896-0x00E4A8B6 */
    if (reply.type == ENTRY_TYPE_NORMAL) {
        entry->type = 1;
        for (i = 0; i < 12; i++) {
            ((uint8_t *)&entry->uid)[i] = reply.data[i];
        }
    } else {
        /* 0x00E4A8B8-0x00E4A8C0 */
        *status_ret = status_$naming_name_not_found;
        entry->type = 0;
    }
}
