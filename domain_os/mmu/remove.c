/*
 * MMU_$REMOVE - Unlink one physical page from the reverse map
 *
 * 0x00E23D64 - 0x00E23D90 (46 bytes, hand-written `MMU_ASM`), and the
 * register-called mmu_$remove_internal at 0x00E23DCC that it and
 * MMU_$REMOVE_LIST `bsr'.  Re-emitted 2026-09-27.
 *
 * The m68k build assembles mmu/sau2/remove.s and mmu/sau2/internal.s
 * (byte-checked against the image); this file is the host-side model of
 * the two routines, compiled only for the host build so the unit tests
 * can drive it.
 *
 * MMU_$REMOVE: (0x14,SP) ppn longword -> D2.  SR saved on the stack,
 * IPL 7, CSR := MMU_$PID_PRIV | 2, mmu_$remove_internal(D2), CSR :=
 * MMU_$PID_PRIV (re-read, not a saved copy), SR restored.
 *
 * mmu_$remove_internal: D2 = ppn; A3 := its PFT entry, then falls into
 * mmu_$remove_pmape (mmu/internal.c).
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$REMOVE(uint32_t ppn)
{
    uint16_t saved_sr;

    DISABLE_INTERRUPTS(saved_sr);                       /* 0x00E23D6C */
    MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;       /* 0x00E23D72 */
    mmu_$remove_internal((uint16_t)ppn);                /* 0x00E23D80 */
    MMU_CSR = MMU_$PID_PRIV;                            /* 0x00E23D82 */
    ENABLE_INTERRUPTS(saved_sr);                        /* 0x00E23D8A */
}

void mmu_$remove_internal(uint16_t ppn)
{
    mmu_$remove_pmape(ppn);                             /* 0x00E23DCC falls into 0x00E23DD8 */
}

#endif /* !ARCH_M68K */
