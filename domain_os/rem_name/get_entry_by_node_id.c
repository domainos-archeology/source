/*
 * rem_name/get_entry_by_node_id.c - REM_NAME_$GET_ENTRY_BY_NODE_ID (0x00E4A800)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$GET_ENTRY_BY_NODE_ID - Look up entry by node ID
 *
 * Queries a remote naming server to find an entry by its node ID.
 *
 * Original address: 0x00e4a800
 * Original size: 204 bytes
 */
void REM_NAME_$GET_ENTRY_BY_NODE_ID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                     uint32_t target_node, void *entry_ret,
                                     status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uint16_t node_high;      /* 0x800 | (node >> 16) | 0x1e00 */
        uint16_t node_low;       /* node & 0xFFFF */
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint16_t *entry = (uint16_t *)entry_ret;
    int16_t entry_type;
    int i;

    request.opcode = REM_NAME_OP_GET_ENTRY_BY_NODE;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.node_high = 0x800 | ((target_node >> 16) & 0xFF) | 0x1e00;
    request.node_low = (uint16_t)target_node;

    if (!rem_name_$send_request(net, node, &request, 0x38, 0, 0x18,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    /* Parse response */
    entry[1] = *(uint16_t *)(response + 0x12 + 2);  /* name length */
    for (i = 0; i < 32; i++) {
        ((uint8_t *)entry)[4 + i] = response[0x12 + 4 + i];
    }

    entry_type = *(int16_t *)(response + 0x12);

    if (entry_type == ENTRY_TYPE_NORMAL) {
        entry[0] = 1;
        for (i = 0; i < 12; i++) {
            ((uint8_t *)entry)[0x24 + i] = response[0x12 + 0x24 + i];
        }
    } else {
        *status_ret = status_$naming_name_not_found;
        entry[0] = 0;
    }
}
