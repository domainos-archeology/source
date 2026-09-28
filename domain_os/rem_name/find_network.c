/*
 * rem_name/find_network.c - REM_NAME_$FIND_NETWORK (0x00E4ADD6, 174 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4ADDE).
 *
 * Look up a network entry by node id on the current name server, locating a
 * server first if the last request failed (last_status != 0), and retrying
 * once from a freshly located server on any error other than "not found".
 *
 * Frame (A6+): 0x08 dir_uid -> A4, 0x0C target_node -> D3 (pointer; the
 * longword it names is pushed by value, `move.l (A0),-(SP)` 0x00E4AE28),
 * 0x10 entry_ret -> A3, 0x14 status_ret -> A2.
 * Locals (A6-): -0x38 node, -0x34 net.  D2b = "server already located".
 */

#include "rem_name/rem_name_internal.h"

void REM_NAME_$FIND_NETWORK(uid_t *dir_uid, uint32_t *target_node,
                            void *entry_ret, status_$t *status_ret)
{
    uint32_t node;                          /* A6-0x38 */
    uint32_t net;                           /* A6-0x34 */
    boolean  located;                       /* D2b */
    status_$t st;

    located = 0;                                            /* 0x00E4ADF4 */

    /* 0x00E4ADF6 `tst.l (0x2c,A5)`: a bad last status forces a locate */
    if (rem_name_$data.last_status != status_$ok) {
        LOCATE_SERVER(&node, &net, status_ret);             /* 0x00E4AE06 */
        if (*status_ret != status_$ok) {
            return;                                         /* 0x00E4AE7A */
        }
        located = (boolean)-1;                              /* 0x00E4AE12 */
    } else {
        node = rem_name_$data.curr_node;                    /* 0x00E4AE16 (0x30,A5) */
        net  = rem_name_$data.curr_net;                     /* 0x00E4AE1C (0x34,A5) */
    }

    /* 0x00E4AE22-0x00E4AE38 */
    REM_NAME_$GET_ENTRY_BY_NODE_ID(net, node, dir_uid, *target_node, entry_ret, status_ret);

    /* 0x00E4AE3C-0x00E4AE4C: not-found and ok are final; so is any error
     * when a server was already located this call. */
    st = *status_ret;
    if (st == status_$naming_name_not_found || st == status_$ok || located < 0) {
        return;
    }

    /* 0x00E4AE4E-0x00E4AE76: locate and retry once */
    LOCATE_SERVER(&node, &net, status_ret);
    if (*status_ret == status_$ok) {
        REM_NAME_$GET_ENTRY_BY_NODE_ID(net, node, dir_uid, *target_node, entry_ret, status_ret);
    }
}
