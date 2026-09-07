/*
 * rem_name/find_network.c - REM_NAME_$FIND_NETWORK (0x00E4ADD6)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$FIND_NETWORK - Find a network entry by node ID
 *
 * Higher-level lookup that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4add6
 * Original size: 174 bytes
 */
void REM_NAME_$FIND_NETWORK(uid_t *dir_uid, uint32_t *target_node,
                            void *entry_ret, status_$t *status_ret)
{
    uint32_t node, net;
    boolean tried_locate = false;

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

    REM_NAME_$GET_ENTRY_BY_NODE_ID(net, node, dir_uid, *target_node, entry_ret, status_ret);

    if (*status_ret == status_$naming_name_not_found ||
        *status_ret == status_$ok) {
        return;
    }

    if (!tried_locate) {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret == status_$ok) {
            REM_NAME_$GET_ENTRY_BY_NODE_ID(net, node, dir_uid, *target_node, entry_ret, status_ret);
        }
    }
}
