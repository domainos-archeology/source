/*
 * MST_$DEALLOCATE_ASID - Deallocate an Address Space ID
 *
 * Original address: 0x00E42E3C (SAU2 map: MST_UNWIRED, E42E3C)
 * Size: 24 bytes (0x00E42E3C .. 0x00E42E53)
 *
 * A forwarding wrapper: the whole body is one call to MST_$FREE_ASID
 * (0x00E74B3C) with the same two arguments.
 *
 * Frame (link.w A6,0x0):
 *   (0x8,A6)   asid         word
 *   (0xa,A6)   status_ret   pointer
 *
 *   0x00E42E40  subq.l #0x2,SP          Pascal result slot for the callee
 *   0x00E42E42  move.l (0xa,A6),-(SP)   status_ret
 *   0x00E42E46  move.w (0x8,A6),-(SP)   asid
 *   0x00E42E4A  jsr MST_$FREE_ASID
 *   0x00E42E50  unlk A6                 (the frame is discarded, no addq)
 *
 * The result slot means the caller was compiled against a declaration of
 * MST_$FREE_ASID as a Pascal function; MST_$FREE_ASID itself leaves D0
 * undefined and this wrapper never reads it, so nothing is returned here.
 *
 * Verified against the disassembly 2026-09-19.
 */

#include "mst/mst_internal.h"

/*
 * @param asid        The ASID to deallocate
 * @param status_ret  Output: status code
 */
void MST_$DEALLOCATE_ASID(uint16_t asid, status_$t *status_ret)
{
    /* 0x00E42E40 .. 0x00E42E4A */
    MST_$FREE_ASID(asid, status_ret);
}
