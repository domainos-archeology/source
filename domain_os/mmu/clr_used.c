/*
 * MMU_$CLR_USED - Clear the REFERENCED bit of a physical page's PFT entry
 *
 * 0x00E2425A - 0x00E2426C (20 bytes, hand-written `MMU_ASM`).  Callers:
 * 0x00E0A300, 0x00E0F368.
 *
 * The m68k build assembles mmu/sau2/clr_used.s (byte-identical to the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 *
 *   0x00E2425A  lea (0xffb800).l,A0
 *   0x00E24260  move.w (0x4,SP),D0w          the FIRST (high) word of the
 *                                            pushed ppn longword
 *   0x00E24264  lsl.w #0x2,D0w               PFT index, as a word
 *   0x00E24266  andi.w #0xdfff,(0x2,A0,D0w)  bit 13 of the entry's second
 *                                            word = PFT_FLAG_REFERENCED in
 *                                            the low half of the longword
 *
 * Both image callers push the ppn as a LONGWORD (`move.l', 0x00E0A2FC and
 * 0x00E0F366), so the routine indexes the PFT by the ppn's HIGH word - 0
 * for every real page - and clears PFT[0]'s REFERENCED bit.  That is the
 * image's behaviour and the C callers reproduce it by passing the long
 * (no Pascal word slot here, unlike the other MMU_ASM routines; mmu/mmu.h,
 * source-nxtd).  TODO(source-qhu6): an original Apollo bug (a 16-bit
 * Pascal declaration called with an integer32), preserved.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$CLR_USED(uint32_t ppn)
{
    uint16_t idx4 = (uint16_t)((ppn >> 16) << 2);                /* 0x00E24260/64 */
    uint32_t *e = (uint32_t *)((char *)PFT_BASE + (int16_t)idx4);

    *e &= ~(uint32_t)PFT_FLAG_REFERENCED;                        /* 0x00E24266 */
}

#endif /* !ARCH_M68K */
