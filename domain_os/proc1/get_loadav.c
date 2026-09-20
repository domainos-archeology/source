/*
 * PROC1_$GET_LOADAV - Get system load averages
 * Original address: 0x00e14bba (32 bytes)
 *
 * Copies the three load-average longwords at the head of the PROC1_ module
 * block (A5 + 0x00, 0x00E254E8) into the caller's record.
 *
 * 0x00E14BBA  link.w A6,0x0 / pea (A5) / lea (0xe254e8).l,A5
 * 0x00E14BC6  lea (A5),A0                   ; &PROC1_$LOADAV[0]
 * 0x00E14BC8  movea.l (0x8,A6),A1           ; argument 1: loadav
 * 0x00E14BCC  move.l (A0)+,(A1)+  x3        ; 1, 5 and 15 minute values
 * 0x00E14BD2  movea.l (-0x4,A6),A5 / unlk A6 / rts
 *
 * Parameters:
 *   loadav - receives PROC1_LOADAV_COUNT (3) longwords
 */

#include "proc1/proc1_internal.h"

void PROC1_$GET_LOADAV(uint32_t *loadav)
{
    /* 0x00E14BCC..0x00E14BD0 */
    loadav[0] = (uint32_t)PROC1_$LOADAV[0];
    loadav[1] = (uint32_t)PROC1_$LOADAV[1];
    loadav[2] = (uint32_t)PROC1_$LOADAV[2];
}
