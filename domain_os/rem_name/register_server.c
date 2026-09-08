/*
 * rem_name/register_server.c - REM_NAME_$REGISTER_SERVER (0x00E4A4AE)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$REGISTER_SERVER - Register that we've heard from a name server
 *
 * Updates the last-heard-from timestamp and sets the server contacted flag.
 *
 * Both arguments are passed by reference by every call site (see
 * rem_name/rem_name.h) and neither is read: the whole body is the two stores
 * below, 0x00E4A4B8 and 0x00E4A4C0.  They are named for what
 * RIP_$ANNOUNCE_NS passes, (&ROUTE_$PORT, &NODE_$ME) at 0x00E69152-0x00E6915E.
 *
 * Original address: 0x00e4a4ae
 * Original size: 26 bytes
 */
void REM_NAME_$REGISTER_SERVER(uint32_t *net, uint32_t *node)
{
    (void)net;   /* pushed by the caller, never read (0x00E4A4AE frame) */
    (void)node;

    rem_name_$data.time_heard_from_server = TIME_$CLOCKH;
    rem_name_$data.heard_from_server = true;
}
