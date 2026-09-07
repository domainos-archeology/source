/*
 * rem_name/dir_readu.c - REM_NAME_$DIR_READU (0x00E4AC2C)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$DIR_READU - Read directory entries with auto server location
 *
 * Higher-level directory read that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4ac2c
 * Original size: 236 bytes
 */
void REM_NAME_$DIR_READU(uid_t *dir_uid, void *entries_ret, int32_t *continuation,
                         uint16_t *max_entries, uint16_t *count_ret,
                         status_$t *status_ret)
{
    uint32_t node, net;
    int16_t entries_read;
    boolean tried_locate = false;

    *count_ret = 0;

    if (*max_entries == 0 || *continuation == 0) {
        *continuation = 0;
        *status_ret = status_$ok;
        return;
    }

    node = rem_name_$data.curr_node;
    net = rem_name_$data.curr_net;

    while (*count_ret < *max_entries) {
        REM_NAME_$READ_DIR(net, node, dir_uid,
                           (uint32_t)(*continuation >> 16),
                           (uint8_t *)entries_ret + (*count_ret * DIR_ENTRY_SIZE),
                           *max_entries - *count_ret, (uint16_t *)&entries_read,
                           status_ret);

        if (*status_ret == status_$ok) {
            tried_locate = true;
            *continuation = (*continuation & 0xFFFF0000) |
                           (((*continuation & 0xFFFF) + entries_read) & 0xFFFF);
            *count_ret += entries_read;
        } else if (*status_ret == status_$naming_cannot_find_entry_in_replicated_root ||
                   *status_ret == status_$naming_last_entry_in_replicated_root_returned) {
            *continuation = 0;
            *count_ret += entries_read;
            *status_ret = status_$ok;
            return;
        } else {
            if (tried_locate || ((*continuation & 0xFFFF) != 1)) {
                *continuation = 0;
                *status_ret = status_$ok;
                return;
            }
            LOCATE_SERVER(&node, &net, status_ret);
            if (*status_ret != status_$ok) {
                *continuation = 0;
                return;
            }
            tried_locate = true;
        }
    }

    *status_ret = status_$ok;
}
