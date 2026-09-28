/*
 * rem_name/find_uid.c - REM_NAME_$FIND_UID (0x00E4AE84, 162 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4AE8C).
 *
 * Look up an entry by UID on the current name server, locating a server
 * first if the last request failed, and retrying once from a freshly located
 * server on any error other than "not found".  Unlike REM_NAME_$FIND_NETWORK
 * and REM_NAME_$GET_ENTRY there is NO "already located" flag: a lookup that
 * fails right after a forced locate still locates again and retries.
 *
 * Frame (A6+): 0x08 dir_uid -> A4, 0x0C target_uid -> D2 (pointer, pushed
 * as is), 0x10 entry_ret -> A3, 0x14 status_ret -> A2.
 * Locals (A6-): -0x8 node, -0x4 net.
 */

#include "rem_name/rem_name_internal.h"

void REM_NAME_$FIND_UID(uid_t *dir_uid, uid_t *target_uid,
                        void *entry_ret, status_$t *status_ret)
{
    uint32_t node;                          /* A6-0x8 */
    uint32_t net;                           /* A6-0x4 */
    status_$t st;

    /* 0x00E4AEA2 `tst.l (0x2c,A5)` */
    if (rem_name_$data.last_status != status_$ok) {
        LOCATE_SERVER(&node, &net, status_ret);             /* 0x00E4AEB2 */
        if (*status_ret != status_$ok) {
            return;                                         /* 0x00E4AF1C */
        }
    } else {
        node = rem_name_$data.curr_node;                    /* 0x00E4AEC0 (0x30,A5) */
        net  = rem_name_$data.curr_net;                     /* 0x00E4AEC6 (0x34,A5) */
    }

    /* 0x00E4AECC-0x00E4AEE0 */
    REM_NAME_$GET_ENTRY_BY_UID(net, node, dir_uid, target_uid, entry_ret, status_ret);

    /* 0x00E4AEE4-0x00E4AEF0 */
    st = *status_ret;
    if (st == status_$naming_name_not_found || st == status_$ok) {
        return;
    }

    /* 0x00E4AEF2-0x00E4AF18: locate and retry once */
    LOCATE_SERVER(&node, &net, status_ret);
    if (*status_ret == status_$ok) {
        REM_NAME_$GET_ENTRY_BY_UID(net, node, dir_uid, target_uid, entry_ret, status_ret);
    }
}
