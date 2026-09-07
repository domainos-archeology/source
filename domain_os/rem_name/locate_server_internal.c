/*
 * rem_name/locate_server_internal.c - LOCATE_SERVER (0x00E4A420)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * LOCATE_SERVER - Internal function to locate a naming server
 *
 * If we've recently heard from a server, uses cached location.
 * Otherwise attempts to locate a server, with retry limiting.
 *
 * Parameters:
 *   node_ret   - Output: node ID of server
 *   net_ret    - Output: network ID of server
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a420
 * Original size: 142 bytes
 */
void LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    uint32_t time_diff;

    if (rem_name_$data.heard_from_server) {
        /* Check if contact is still recent */
        time_diff = rem_name_$data.time_heard_from_server - TIME_$CLOCKH;
        if ((int32_t)time_diff < 0) {
            time_diff = -time_diff;
        }

        if (time_diff > rem_name_$data.server_timeout) {
            /* Too long since last contact - need to relocate */
            rem_name_$data.heard_from_server = false;
            rem_name_$data.last_status = status_$naming_cant_find_name_server_helper;
            *status_ret = status_$naming_cant_find_name_server_helper;
            return;
        }

        /* Use cached server - call REM_NAME_$LOCATE_SERVER */
        REM_NAME_$LOCATE_SERVER(node_ret, net_ret, status_ret);
    } else {
        /* Haven't heard from server recently */
        if (rem_name_$data.retry_count > 3) {
            /* Too many retries */
            *status_ret = status_$naming_cant_find_name_server_helper;
            return;
        }

        rem_name_$data.retry_count++;
        REM_NAME_$LOCATE_SERVER(node_ret, net_ret, status_ret);

        if (*status_ret == status_$ok) {
            rem_name_$data.heard_from_server = true;
            rem_name_$data.time_heard_from_server = TIME_$CLOCKH;
        }
    }
}
