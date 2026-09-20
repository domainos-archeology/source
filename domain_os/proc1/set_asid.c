/*
 * PROC1_$SET_ASID - Give the current process a new address space id
 * Original address: 0x00e148f8 (38 bytes)
 *
 * 0x00E148F8  link.w A6,0x0 / move.l D2,-(SP)
 * 0x00E148FE  D2 = asid (0x8,A6)
 * 0x00E14902  A0 = PROC1_$CURRENT_PCB (0xE1EAC8); (0x46,A0) = D2
 * 0x00E1490C  subq.l #2,SP / move.w D2,-(SP)      Pascal result slot + arg
 * 0x00E14910  jsr MMU_$INSTALL_ASID (0x00E24204)   result discarded; no
 *                                                  cleanup: the unlk does it
 * 0x00E14916  move.l (-0x4,A6),D2 / unlk / rts
 *
 * Parameters:
 *   asid - the address space id to record in the PCB and load into the MMU
 */

#include "proc1/proc1_internal.h"
#include "mmu/mmu.h"

void PROC1_$SET_ASID(uint16_t asid)
{
    /* 0x00E14902 / 0x00E14908 */
    PROC1_$CURRENT_PCB->asid = asid;

    /* 0x00E1490C..0x00E14910 */
    MMU_$INSTALL_ASID(asid);
}
