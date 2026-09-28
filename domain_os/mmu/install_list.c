/*
 * MMU_$INSTALL_LIST - Install a run of pages at consecutive virtual addresses
 *
 * 0x00E23FDE - 0x00E24046 (106 bytes, hand-written `MMU_ASM`).  Re-emitted
 * from the disassembly on 2026-09-22.  Wrong before: an invented
 * `count == 0` early return (the image runs `subq.w #1` / `dbf`, so a zero
 * count installs 65536 pages), the CSR was restored from a saved copy
 * (the image re-reads MMU_$PID_PRIV, 0x00E24038), and the per-page packed
 * value was advanced with a longword add (the image uses `add.w #0x10`,
 * 0x00E2402C, so the carry never leaves the low word).
 *
 * Arguments (after the ten-register movem, 0x2c off SP):
 *   (0x2c,SP) count      word (D7)
 *   (0x2e,SP) ppn_array  -> longwords, low word used (A5)
 *   (0x32,SP) va         longword (A4)
 *   (0x36,SP) flags      longword: byte +1 = asid, byte +3 = prot
 *
 * D5 = ((va << MMU_$PTT_SHIFT) with prot in the low byte) ror 5, then asid
 * in the low byte, ror 7; on a 68010 (M68020 word zero) the low word is
 * shifted right 2; the low nibble is cleared.  Interrupts off (SR saved in
 * D6), CSR = MMU_$PID_PRIV | 2, then for each page mmu_$installi(D2 = ppn,
 * A4 = va, D4 = D5), D5w += 0x10, A4 += 0x400.
 *
 * The m68k build assembles mmu/sau2/install_list.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$INSTALL_LIST(uint16_t count, uint32_t *ppn_array, uint32_t va, uint32_t flags)
{
    uint16_t saved_sr;              /* D6 */
    uint32_t packed;                /* D5 */
    uint16_t n;                     /* D7 */
    uint8_t prot = (uint8_t)(flags & 0xFF);          /* (0x39,SP) */
    uint8_t asid = (uint8_t)((flags >> 16) & 0xFF);  /* (0x37,SP) */

    /* 0x00E23FEA - 0x00E23FF0 */
    n = (uint16_t)(count - 1);

    /* 0x00E23FF2 - 0x00E24010 */
    packed = va << (MMU_$PTT_SHIFT & 0x3F);
    packed = (packed & 0xFFFFFF00u) | prot;
    packed = (packed >> 5) | (packed << 27);
    packed = (packed & 0xFFFFFF00u) | asid;
    packed = (packed >> 7) | (packed << 25);
    if (!M68020_IS_020_W()) {
        packed = (packed & 0xFFFF0000u) | ((packed & 0xFFFF) >> 2);
    }
    packed &= 0xFFFFFFF0u;

    /* 0x00E24012 - 0x00E24024 */
    DISABLE_INTERRUPTS(saved_sr);
    MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;

    /* 0x00E24026 - 0x00E24034: dbf = n + 1 pages */
    do {
        mmu_$installi((uint16_t)*ppn_array, va, packed);
        ppn_array++;
        packed = (packed & 0xFFFF0000u) | ((packed + 0x10) & 0xFFFF);
        va += 0x400;
    } while (n-- != 0);

    /* 0x00E24038 - 0x00E24040 */
    MMU_CSR = MMU_$PID_PRIV;
    ENABLE_INTERRUPTS(saved_sr);
}

#endif /* !ARCH_M68K */
