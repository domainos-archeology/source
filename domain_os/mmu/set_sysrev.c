/*
 * MMU_$SET_SYSREV - Latch the hardware revision byte into MMU_$SYSTEM_REV
 *
 * 0x00E24272 - 0x00E2427E (14 bytes, hand-written `MMU_ASM`), preceded by
 * the four-byte MMU_$SYSTEM_REV cell at 0x00E2426E.  Re-emitted
 * 2026-09-27: `lea (-0x6,PC),A0` addresses 0xE2426E and the byte from
 * 0xFFB409 is stored at (3,A0) = 0xE24271, the LOW byte of
 * MMU_$SYSTEM_REV - not a separate cell at 0xE2426F as mmu.h used to say.
 *
 * The m68k build assembles mmu/sau2/set_sysrev.s (byte-identical, and
 * the definition of MMU_$SYSTEM_REV); this is the host-side model.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$SET_SYSREV(void)
{
    MMU_$SYSTEM_REV = (MMU_$SYSTEM_REV & 0xFFFFFF00u) | DN330_MMU_HARDWARE_REV; /* 0x00E24276 */
}

#endif /* !ARCH_M68K */
