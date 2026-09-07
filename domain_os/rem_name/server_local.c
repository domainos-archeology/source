/*
 * rem_name/server_local.c - REM_NAME_SERVER_LOCAL (0x00E4A408)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_SERVER_LOCAL - Check if the naming server is local
 *
 * Examines a flag in the event count structure to determine if
 * the naming server is running on the local node.
 *
 * Returns:
 *   0xFF (true) if local, 0 (false) if remote
 *
 * Original address: 0x00e4a408
 * Original size: 24 bytes
 */
boolean REM_NAME_SERVER_LOCAL(void)
{
    /*
     * 0x00E4A40C: movea.l (0x00e28dd8).l,A0 loads slot 10 of the SOCK socket
     * pointer table; (0x16,A0) is sock_$sock_t.flags and btst #13 / sne gives
     * the 0xFF/0x00 Domain boolean.
     */
    sock_$sock_t *sock =
        (sock_$sock_t *)SOCK_$EVENT_COUNTERS[REM_NAME_$SOCK - 1];

    return ((sock->flags & SOCK_FLAG_SERVER_LOCAL) != 0) ? true : false;
}
