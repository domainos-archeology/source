/*
 * MMU_$INSTALL_PRIVATE - Install one page without the global bit
 *
 * 0x00E23F82 - 0x00E23FDC (92 bytes, hand-written `MMU_ASM`).  Re-emitted
 * from the disassembly on 2026-09-22.  Wrong before: the global bit was
 * cleared through a byte-pointer cast into the PFT entry, and the CSR was
 * restored from a saved copy (the image re-reads MMU_$PID_PRIV,
 * 0x00E23FCE).
 *
 * Arguments (after the nine-register movem, 0x28 off SP):
 *   (0x28,SP) ppn    longword (D2), low word used
 *   (0x2c,SP) va     longword (A4)
 *   (0x30,SP) flags  longword: byte +1 = asid, byte +3 = prot
 *
 * The packed word is built exactly as in MMU_$INSTALL_LIST; after
 * mmu_$installi (which leaves A3 = the PFT entry of ppn) the entry's low
 * word has bit 12 cleared (`andi.w #-0x1001,(0x2,A3)`, 0x00E23FC8).
 *
 * The m68k build assembles mmu/sau2/install_private.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$INSTALL_PRIVATE(uint32_t ppn, uint32_t va, uint32_t flags)
{
    uint16_t saved_sr;              /* D6 */
    uint32_t packed;                /* D4 */
    uint8_t prot = (uint8_t)(flags & 0xFF);          /* (0x33,SP) */
    uint8_t asid = (uint8_t)((flags >> 16) & 0xFF);  /* (0x31,SP) */

    /* 0x00E23F8E - 0x00E23FAC */
    packed = va << (MMU_$PTT_SHIFT & 0x3F);
    packed = (packed & 0xFFFFFF00u) | prot;
    packed = (packed >> 5) | (packed << 27);
    packed = (packed & 0xFFFFFF00u) | asid;
    packed = (packed >> 7) | (packed << 25);
    if (!M68020_IS_020_W()) {
        packed = (packed & 0xFFFF0000u) | ((packed & 0xFFFF) >> 2);
    }
    packed &= 0xFFFFFFF0u;

    /* 0x00E23FB0 - 0x00E23FBE */
    DISABLE_INTERRUPTS(saved_sr);
    MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;

    /* 0x00E23FC4 */
    mmu_$installi((uint16_t)ppn, va, packed);

    /* 0x00E23FC8: bit 12 of the entry's low word */
    *PFT_FOR_PPN((uint16_t)ppn) &= ~(uint32_t)PFT_FLAG_GLOBAL;

    /* 0x00E23FCE - 0x00E23FD6 */
    MMU_CSR = MMU_$PID_PRIV;
    ENABLE_INTERRUPTS(saved_sr);
}

#endif /* !ARCH_M68K */
