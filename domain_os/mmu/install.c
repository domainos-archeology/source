/*
 * MMU_$INSTALL - Install one page, global bit left as mmu_$installi sets it
 *
 * 0x00E24048 - 0x00E2409A (84 bytes, hand-written `MMU_ASM`).  Callers:
 * PEB_$INIT, PEB_$ASSOC, MST_$INIT, mst_$init_table_page and others.
 *
 * The m68k build assembles mmu/sau2/install.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 *
 * Arguments (after the nine-register movem, 0x28 off SP):
 *   (0x28,SP) ppn    longword (D2), low word used
 *   (0x2c,SP) va     longword (A4)
 *   (0x30,SP) flags  longword: byte +1 = asid, byte +3 = prot
 *
 * The packed word is built exactly as in MMU_$INSTALL_PRIVATE
 * (0x00E24054-0x00E24072): (va << MMU_$PTT_SHIFT) with prot in the low
 * byte, ror 5, asid in the low byte, ror 7; on a 68010 (M68020 word zero,
 * 0x00E24068) the low word is shifted right 2; `and.w #0xfff0' clears the
 * low nibble.  SR saved in D6, IPL 7, CSR := MMU_$PID_PRIV | 2
 * (0x00E24076-0x00E24084), mmu_$installi (0x00E2408A), CSR := MMU_$PID_PRIV
 * re-read from the cell (0x00E2408C), SR restored.  Unlike
 * MMU_$INSTALL_PRIVATE nothing touches the entry afterwards, so the global
 * bit mmu_$installi sets for ASID 0 stays set.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    uint16_t saved_sr;              /* D6 */
    uint32_t packed;                /* D4 */
    uint8_t prot = (uint8_t)(flags & 0xFF);          /* (0x33,SP) */
    uint8_t asid = (uint8_t)((flags >> 16) & 0xFF);  /* (0x31,SP) */

    /* 0x00E24054 - 0x00E24072 */
    packed = va << (MMU_$PTT_SHIFT & 0x3F);
    packed = (packed & 0xFFFFFF00u) | prot;
    packed = (packed >> 5) | (packed << 27);
    packed = (packed & 0xFFFFFF00u) | asid;
    packed = (packed >> 7) | (packed << 25);
    if (!M68020_IS_020_W()) {
        packed = (packed & 0xFFFF0000u) | ((packed & 0xFFFF) >> 2);
    }
    packed &= 0xFFFFFFF0u;

    /* 0x00E24076 - 0x00E24084 */
    DISABLE_INTERRUPTS(saved_sr);
    MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;

    /* 0x00E2408A */
    mmu_$installi((uint16_t)ppn, va, packed);

    /* 0x00E2408C - 0x00E24094 */
    MMU_CSR = MMU_$PID_PRIV;
    ENABLE_INTERRUPTS(saved_sr);
}

#endif /* !ARCH_M68K */
