/*
 * MSG_$GET_MY_NODE - Get local node ID
 *
 * Original address: 0x00E5912E (18 bytes)
 *
 * Assembly:
 *   00e5912e  link.w A6,0x0
 *   00e59132  movea.l (0x8,A6),A0
 *   00e59136  move.l (0x00e245a4).l,(A0)
 *   00e5913c  unlk A6 / rts
 */

#include "msg/msg_internal.h"
#include "uid/uid.h"        /* NODE_$ME (0xE245A4) */

void MSG_$GET_MY_NODE(uint32_t *node_id)
{
    *node_id = NODE_$ME;
}
