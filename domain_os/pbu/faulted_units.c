/*
 * PBU_$FAULTED_UNITS - Report faulted PBU units
 *
 * Stub in this SAU2 image: stores status_$pbu_not_present (0x1E000A, "pbu
 * not present") through the status argument and returns a 16-bit zero.
 *
 * Original address: 0x00E590FA, size 20 bytes (SAU2 map: PBU module)
 *
 *   00e590fa    link.w A6,-0x4
 *   00e590fe    movea.l (0x8,A6),A0       ; arg 1 = status_ret
 *   00e59102    move.l #0x1e000a,(A0)     ; *status_ret = pbu not present
 *   00e59108    clr.w D0w                 ; 16-bit result = 0
 *   00e5910a    unlk A6
 *   00e5910c    rts
 */

#include "pbu/pbu_internal.h"

uint16_t PBU_$FAULTED_UNITS(status_$t *status_ret)
{
    /* 0x00E590FE-0x00E59102 */
    *status_ret = status_$pbu_not_present;
    /* 0x00E59108 */
    return 0;
}
