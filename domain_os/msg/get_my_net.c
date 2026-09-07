/*
 * MSG_$GET_MY_NET - Get local network ID
 *
 * Original address: 0x00E5911C (18 bytes)
 *
 * Assembly:
 *   00e5911c  link.w A6,0x0
 *   00e59120  movea.l (0x8,A6),A0
 *   00e59124  move.l (0x00e2e0a0).l,(A0)
 *   00e5912a  unlk A6 / rts
 *
 * 0xE2E0A0 is ROUTE_$PORT, i.e. ROUTE_$PORT_ARRAY[0].network.
 */

#include "msg/msg_internal.h"

void MSG_$GET_MY_NET(uint32_t *net_id)
{
    *net_id = ROUTE_$PORT;
}
