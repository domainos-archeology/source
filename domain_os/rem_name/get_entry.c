/*
 * rem_name/get_entry.c - REM_NAME_$GET_ENTRY (0x00E4AD18)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$GET_ENTRY - Get a directory entry with auto server location
 *
 * Higher-level entry lookup that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4ad18
 * Original size: 190 bytes
 */
void REM_NAME_$GET_ENTRY(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret)
{
    uint32_t node, net;
    boolean tried_locate = false;
    status_$t status;

    if (rem_name_$data.last_status == status_$ok) {
        node = rem_name_$data.curr_node;
        net = rem_name_$data.curr_net;
    } else {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
        tried_locate = true;
    }

    REM_NAME_$GET_ENTRY_BY_NAME(net, node, dir_uid, name, *name_len, entry_ret, status_ret);
    status = *status_ret;

    if (status == status_$naming_name_not_found ||
        status == status_$naming_invalid_pathname ||
        status == status_$ok) {
        return;
    }

    /* Error - try to relocate server */
    if (!tried_locate) {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret == status_$ok) {
            REM_NAME_$GET_ENTRY_BY_NAME(net, node, dir_uid, name, *name_len, entry_ret, status_ret);
        }
    }
}
