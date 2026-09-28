/*
 * MMU_$PTOV - Physical page number to virtual address
 *
 * 0x00E241B8 - 0x00E241F2 (60 bytes, hand-written `MMU_ASM`).  Re-emitted
 * from the disassembly on 2026-09-22: the 68010 path shifted the low word
 * left 2 as a longword; the image's `lsl.w #0x2,D0w` (0x00E241EA) drops
 * anything carried out of bit 15.
 *
 * Argument: (0x4,SP) ppn, longword (D1).
 *   D1w = ppn << 2 (word); D0 = PFT[D1w] (longword, D1w sign-extended as
 *   the index); if (D0w & 0xfff) == 0 return 0.
 *   D0 &= 0xF0000; D0w = ASID_TABLE[ppn];
 *   68020 (high byte of M68020): return D0 << 6
 *   68010: D0w <<= 2; return D0 << 4
 *
 * The m68k build assembles mmu/sau2/ptov.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

uint32_t MMU_$PTOV(uint32_t ppn)
{
    uint16_t idx = (uint16_t)(ppn << 2);                            /* D1w */
    uint32_t v = *(uint32_t *)((char *)PFT_BASE + (int16_t)idx);    /* 0x00E241C4 */

    if ((v & 0x0FFF) == 0) {                                        /* 0x00E241C8 */
        return 0;
    }

    /* 0x00E241CE - 0x00E241DC */
    v &= 0x000F0000u;
    v |= *(uint16_t *)((char *)ASID_TABLE_BASE + (int16_t)(idx >> 1));

    if (M68020_IS_020_B()) {                                        /* 0x00E241E0 */
        return v << 6;
    }
    v = (v & 0xFFFF0000u) | (((v & 0xFFFF) << 2) & 0xFFFF);         /* 0x00E241EA */
    return v << 4;
}

#endif /* !ARCH_M68K */
