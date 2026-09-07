/*
 * rem_name/find_uid.c - REM_NAME_$FIND_UID (0x00E4AE84)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$FIND_UID - Find an object by UID
 *
 * Higher-level lookup that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4ae84
 * Original size: 162 bytes
 */
void REM_NAME_$FIND_UID(uid_t *dir_uid, uid_t *target_uid,
                        void *entry_ret, status_$t *status_ret)
{
    uint32_t node, net;

    if (rem_name_$data.last_status == status_$ok) {
        node = rem_name_$data.curr_node;
        net = rem_name_$data.curr_net;
    } else {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
    }

    REM_NAME_$GET_ENTRY_BY_UID(net, node, dir_uid, target_uid, entry_ret, status_ret);

    if (*status_ret == status_$naming_name_not_found ||
        *status_ret == status_$ok) {
        return;
    }

    LOCATE_SERVER(&node, &net, status_ret);
    if (*status_ret == status_$ok) {
        REM_NAME_$GET_ENTRY_BY_UID(net, node, dir_uid, target_uid, entry_ret, status_ret);
    }
}
