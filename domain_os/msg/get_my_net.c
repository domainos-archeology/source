/*
 * MSG_$GET_MY_NET - Get local network ID
 *
 * Returns the local network ID from the routing table.
 *
 * Original address: 0x00E5911C
 * Original size: 18 bytes
 */

#include "msg/msg_internal.h"

void MSG_$GET_MY_NET(uint32_t *net_id)
{
#if defined(ARCH_M68K)
    /* Return ROUTE__PORT (network ID at 0xE2E0A0) */
    *net_id = *(uint32_t *)0xE2E0A0;
#else
    *net_id = 0;
#endif
}
