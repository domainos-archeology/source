/*
 * MSG_$GET_MY_NODE - Get local node ID
 *
 * Returns the local node ID.
 *
 * Original address: 0x00E5912E
 * Original size: 18 bytes
 */

#include "msg/msg_internal.h"

void MSG_$GET_MY_NODE(uint32_t *node_id)
{
#if defined(M68K)
    /* Return NODE__ME (node ID at 0xE245A4) */
    *node_id = *(uint32_t *)0xE245A4;
#else
    *node_id = 0;
#endif
}
