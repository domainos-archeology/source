/*
 * rem_name/get_entry_by_uid.c - REM_NAME_$GET_ENTRY_BY_UID (0x00E4A8CC)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$GET_ENTRY_BY_UID - Look up entry by UID
 *
 * Queries a remote naming server to find an entry by its UID.
 *
 * Original address: 0x00e4a8cc
 * Original size: 184 bytes
 */
void REM_NAME_$GET_ENTRY_BY_UID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                 uid_t *target_uid, void *entry_ret,
                                 status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uid_t    target_uid;
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint16_t *entry = (uint16_t *)entry_ret;
    int16_t entry_type;
    int i;

    request.opcode = REM_NAME_OP_GET_ENTRY_BY_UID;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.target_uid.high = target_uid->high;
    request.target_uid.low = target_uid->low;

    if (!rem_name_$send_request(net, node, &request, 0x3a, 0, 0x1c,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    /* Parse response */
    entry[1] = *(uint16_t *)(response + 0x12 + 2);
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
