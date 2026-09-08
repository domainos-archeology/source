/*
 * ASKNODE_$INFO - Get node information, with both length arguments defaulted
 *
 * A five-argument wrapper for ASKNODE_$INTERNET_INFO: it forwards the caller's
 * arguments and supplies the routing/request-length and the reply-length
 * arguments from two constants that live in the code region right behind it.
 *
 * Original address: 0x00E64598..0x00E645BC (38 bytes)
 *
 * Assembly (the last push is argument 1):
 *   00e64598  link.w A6,0x0
 *   00e6459c  move.l (0x18,A6),-(SP)   ; arg 7  status
 *   00e645a0  move.l (0x14,A6),-(SP)   ; arg 6  result
 *   00e645a4  pea (0x18,PC)            ; arg 5  &asknode_$default_resp_len
 *                                      ;        0x00E645A6 + 0x18 = 0x00E645BE
 *   00e645a8  move.l (0x10,A6),-(SP)   ; arg 4  param
 *   00e645ac  pea (0x12,PC)            ; arg 3  &asknode_$default_req_len
 *                                      ;        0x00E645AE + 0x12 = 0x00E645C0
 *   00e645b0  move.l (0xc,A6),-(SP)    ; arg 2  node_id
 *   00e645b4  move.l (0x8,A6),-(SP)    ; arg 1  req_type
 *   00e645b8  bsr.b 0x00e645ea         ; ASKNODE_$INTERNET_INFO
 *   00e645ba  unlk A6
 *   00e645bc  rts
 *
 * The argument block is never popped - "unlk A6" discards it - and the
 * INTERNET_INFO return value is dropped, so this wrapper returns void.
 */

#include "asknode/asknode_internal.h"

/*
 * The two-cell constant pool at 0x00E645BE..0x00E645C3, between the "rts" of
 * ASKNODE_$INFO and the "link.w" of ASKNODE_$GET_INFO.  `gsk read 0xE645B0 32`:
 *
 *   00e645b0  2f 2e 00 0c 2f 2e 00 08  61 30 4e 5e 4e 75 00 98
 *                                                        ^0xE645BE
 *   00e645c0  ff ff ff ff 4e 56 00 00  2f 2e 00 1c 2f 2e 00 18
 *             ^0xE645C0               ^0xE645C4 = GET_INFO entry
 *
 * The SAU2 map exports no symbol for either cell.  The pool is shared: this
 * function points argument 5 at 0x00E645BE and argument 3 at 0x00E645C0, and
 * ASKNODE_$GET_INFO points its argument 3 at 0x00E645C0 as well
 * ("pea (-0x1a,PC)" at 0x00E645D8).  ASKNODE_$INTERNET_INFO only ever READS
 * through both pointers (asknode/internet_info.c: "*req_len" and "*resp_len"),
 * so each caller can carry its own copy; they are const here for that reason
 * and the cast at the call site is what strips it.
 */

/* 0x00E645C0, bytes ff ff ff ff.  INTERNET_INFO's request-0x1F arm treats -1
 * as "no routing hint supplied": it substitutes 0 and, once NETWORK_$RING_INFO
 * fails with status_$network_transmit_failed, re-derives a route through
 * DIR_$FIND_NET and retries.  A zero here would disable that retry. */
static const int32_t asknode_$default_req_len = -1;

/* 0x00E645BE, bytes 00 98.  The reply-buffer ceiling INTERNET_INFO hands
 * PKT_$SAR_INTERNET as the reply-data maximum (0x98 = 152 bytes). */
static const uint16_t asknode_$default_resp_len = 0x0098;

void ASKNODE_$INFO(uint16_t *req_type, uint32_t *node_id,
                   uid_t *param, uint32_t *result, status_$t *status)
{
    ASKNODE_$INTERNET_INFO(req_type, node_id,
                           (int32_t *)&asknode_$default_req_len,
                           param,
                           (uint16_t *)&asknode_$default_resp_len,
                           result, status);
}
