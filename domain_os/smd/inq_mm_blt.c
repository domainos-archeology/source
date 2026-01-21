/*
 * smd/inq_mm_blt.c - SMD_$INQ_MM_BLT implementation
 *
 * Inquire memory-to-memory BLT capability.
 *
 * Original address: 0x00E6EE0C
 *
 * Assembly:
 *   link.w A6,0x0
 *   pea (A5)
 *   lea (0xe82b8c).l,A5
 *   movea.l (0x8,A6),A0    ; status_ret
 *   clr.l (A0)              ; *status_ret = 0 (success)
 *   clr.b D0b               ; return 0 (false - not supported)
 *   movea.l (-0x8,A6),A5
 *   unlk A6
 *   rts
 */

#include "smd/smd_internal.h"

/*
 * SMD_$INQ_MM_BLT - Inquire memory-to-memory BLT capability
 *
 * Returns whether memory-to-memory BLT operations are supported.
 * In this implementation, they are not supported (returns 0).
 *
 * Parameters:
 *   status_ret - Output: status return (always status_$ok)
 *
 * Returns:
 *   0 (false) - memory-to-memory BLT not supported
 */
int8_t SMD_$INQ_MM_BLT(status_$t *status_ret)
{
    *status_ret = status_$ok;
    return 0;  /* Not supported */
}
