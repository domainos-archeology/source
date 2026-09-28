/*
 * MMU_$CACHE_INHIBIT_VA - No-op on the SAU2
 *
 * 0x00E2429E - 0x00E2429F (2 bytes, hand-written `MMU_ASM`): a bare `rts'
 * (4e 75).  The one longword argument the callers push is never read.
 *
 * The m68k build assembles mmu/sau2/cache_inhibit_va.s (byte-identical);
 * this file is the host-side model, compiled only for the host build.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$CACHE_INHIBIT_VA(uint32_t va)
{
    (void)va;                                           /* 0x00E2429E rts */
}

#endif /* !ARCH_M68K */
