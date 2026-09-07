/*
 * rem_name/get_entry_by_name.c - REM_NAME_$GET_ENTRY_BY_NAME (0x00E4A588)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$GET_ENTRY_BY_NAME - Look up a directory entry by name
 *
 * Queries a remote naming server to resolve a name within a directory.
 *
 * Parameters:
 *   net        - Network ID
 *   node       - Node ID
 *   dir_uid    - UID of the directory to search
 *   name       - Name to look up
 *   name_len   - Length of name (max 32)
 *   entry_ret  - Output: entry information
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a588
 * Original size: 264 bytes
 */
void REM_NAME_$GET_ENTRY_BY_NAME(uint32_t net, uint32_t node, uid_t *dir_uid,
                                  char *name, uint16_t name_len,
                                  void *entry_ret, status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uint16_t name_len;
        char     name[32];
    } request;

    uint8_t response[0x16a];
    uint8_t out_param[8];
    int16_t resp_len;
    uint16_t *entry = (uint16_t *)entry_ret;
    int16_t entry_type;
    int i;

    if (name_len > 32) {
        *status_ret = status_$naming_invalid_pathname;
        return;
    }

    /* Build request */
    request.opcode = REM_NAME_OP_GET_ENTRY_BY_NAME;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.name_len = name_len;

    /* Copy name */
    for (i = 0; i < name_len; i++) {
        request.name[i] = name[i];
    }

    /* Make remote call */
    if (!rem_name_$send_request(net, node, &request, 0x34 + name_len, 0, 2,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    /* Parse response - copy name length and name (32 bytes at offset 4) */
    entry[1] = *(uint16_t *)(response + 0x12 + 2);  /* name length */
    for (i = 0; i < 32; i++) {
        ((uint8_t *)entry)[4 + i] = response[0x12 + 4 + i];
    }

    entry_type = *(int16_t *)(response + 0x12);

    if (entry_type == ENTRY_TYPE_NORMAL) {
        entry[0] = 1;
        /* Copy additional 12 bytes of data (UID + type info) */
        for (i = 0; i < 12; i++) {
            ((uint8_t *)entry)[0x24 + i] = response[0x12 + 0x24 + i];
        }
    } else if (entry_type == ENTRY_TYPE_LINK) {
        entry[0] = ENTRY_TYPE_LINK_ALT;
        /* Set to UID_$NIL */
        *(uint32_t *)(entry + 0x12) = UID_$NIL.high;
        *(uint32_t *)(entry + 0x14) = UID_$NIL.low;
        *(uint32_t *)(entry + 0x16) = 0;
    } else {
        *status_ret = status_$naming_name_not_found;
        entry[0] = 0;
    }
}
