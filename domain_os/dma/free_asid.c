/*
 * dma/free_asid.c - DMA_$FREE_ASID
 *
 * Original address: 0x00E0A454
 * Original size: 2 bytes
 *
 * The whole routine in the SR10.2 SAU2 image is a single instruction:
 *
 *   00e0a454  4e 75        rts
 *
 * i.e. the DMA subsystem has no per-address-space state to release, and the
 * per-ASID teardown hook is a no-op stub.  It is named by the SR10.2 SAU2
 * link map (sau2-maps/domain_os.10.2.map, "E0A454  DMA_$FREE_ASID"); Ghidra
 * had it as FUN_00e0a454.  It sits at the very end of the DMA_ module, whose
 * next symbol is FIM_$BUILD_DF at E0A458.
 *
 * The one caller is PROC2_$DELETE_CLEANUP, which passes the ASID as a word
 * in the Domain Pascal external-call shape used for every other
 * <NS>_$FREE_ASID in that teardown sequence:
 *
 *   00e74438  subq.l #0x2,SP
 *   00e7443a  move.w (-0xb0,A6),-(SP)     ; the ASID
 *   00e7443e  jsr 0x00e0a454.l            ; DMA_$FREE_ASID
 *   00e74444  addq.w #0x4,SP
 *
 * The stub never reads the argument, so the parameter is typed to match its
 * siblings (SMD_$FREE_ASID, ACL_$FREE_ASID) rather than recovered from use.
 */

#include "dma/dma_internal.h"

void DMA_$FREE_ASID(int16_t asid)
{
    (void)asid;
}
