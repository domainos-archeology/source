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
 * Original address: 0x00e4a4ae
 * Original size: 26 bytes
 */
void REM_NAME_$REGISTER_SERVER(void)
{
    rem_name_$data.time_heard_from_server = TIME_$CLOCKH;
    rem_name_$data.heard_from_server = true;
}
