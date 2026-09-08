/*
 * ASKNODE_$GET_INFO - Get node information, with the caller supplying the
 * reply-length limit
 *
 * A six-argument wrapper for ASKNODE_$INTERNET_INFO.  It differs from
 * ASKNODE_$INFO only in that the caller passes the reply-length argument;
 * the routing/request-length argument still comes from the shared constant
 * cell behind ASKNODE_$INFO.
 *
 * Original address: 0x00E645C4..0x00E645E8 (38 bytes)
 *
 * Assembly (the last push is argument 1):
 *   00e645c4  link.w A6,0x0
 *   00e645c8  move.l (0x1c,A6),-(SP)   ; arg 7  status
 *   00e645cc  move.l (0x18,A6),-(SP)   ; arg 6  result
 *   00e645d0  move.l (0x14,A6),-(SP)   ; arg 5  resp_len
 *   00e645d4  move.l (0x10,A6),-(SP)   ; arg 4  param
 *   00e645d8  pea (-0x1a,PC)           ; arg 3  &asknode_$default_req_len
 *                                      ;        0x00E645DA - 0x1A = 0x00E645C0
 *   00e645dc  move.l (0xc,A6),-(SP)    ; arg 2  node_id
 *   00e645e0  move.l (0x8,A6),-(SP)    ; arg 1  req_type
 *   00e645e4  bsr.b 0x00e645ea         ; ASKNODE_$INTERNET_INFO
 *   00e645e6  unlk A6
 *   00e645e8  rts
 *
 * The argument block is never popped - "unlk A6" discards it - and the
 * INTERNET_INFO return value is dropped, so this wrapper returns void.
 */

#include "asknode/asknode_internal.h"

/*
 * 0x00E645C0, bytes ff ff ff ff - the second cell of the two-cell pool at
 * 0x00E645BE..0x00E645C3 that sits between ASKNODE_$INFO's "rts" and this
 * function's "link.w".  `gsk read 0xE645B0 32`:
 *
 *   00e645b0  2f 2e 00 0c 2f 2e 00 08  61 30 4e 5e 4e 75 00 98
 *   00e645c0  ff ff ff ff 4e 56 00 00  2f 2e 00 1c 2f 2e 00 18
 *             ^this cell              ^0xE645C4 = this function's entry
 *
 * The image shares the one cell with ASKNODE_$INFO ("pea (0x12,PC)" at
 * 0x00E645AC); since ASKNODE_$INTERNET_INFO only reads through the pointer
 * (asknode/internet_info.c: "*req_len"), each caller carrying its own copy is
 * behaviour-preserving.  The SAU2 map exports no symbol for the cell.
 *
 * INTERNET_INFO's request-0x1F arm treats -1 as "no routing hint supplied":
 * it substitutes 0 and, once NETWORK_$RING_INFO fails with
 * status_$network_transmit_failed, re-derives a route through DIR_$FIND_NET
 * and retries.  A zero here would disable that retry.
 */
static const int32_t asknode_$default_req_len = -1;

void ASKNODE_$GET_INFO(uint16_t *req_type, uint32_t *node_id,
                       uid_t *param, uint16_t *resp_len,
                       uint32_t *result, status_$t *status)
{
    ASKNODE_$INTERNET_INFO(req_type, node_id,
                           (int32_t *)&asknode_$default_req_len,
                           param, resp_len, result, status);
}
