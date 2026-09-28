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
 *   0x00E24260  move.w (0x4,SP),D0w          the LOW word of the pushed ppn
 *   0x00E24264  lsl.w #0x2,D0w               PFT index, as a word
 *   0x00E24266  andi.w #0xdfff,(0x2,A0,D0w)  bit 13 of the entry's second
 *                                            word = PFT_FLAG_REFERENCED in
 *                                            the low half of the longword
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$CLR_USED(uint32_t ppn)
{
    uint16_t idx4 = (uint16_t)(ppn << 2);                        /* 0x00E24264 */
    uint32_t *e = (uint32_t *)((char *)PFT_BASE + (int16_t)idx4);

    *e &= ~(uint32_t)PFT_FLAG_REFERENCED;                        /* 0x00E24266 */
}

#endif /* !ARCH_M68K */
