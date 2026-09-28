/*
 * MMU_$POWER_OFF - Is the orderly-shutdown bit of the FPU-owner/power word set?
 *
 * 0x00E2428C - 0x00E2429C (18 bytes, hand-written `MMU_ASM`):
 *   move.w (0xFFB402),D0w / eori.w #0x0,D0w / btst.l #0x2,D0 / sne D0b / rts
 * Verified against the disassembly on 2026-09-22; faithful.  The `eori #0`
 * is a no-op left in the image; bit 2 of the WORD is tested.
 *
 * The m68k build assembles mmu/sau2/power_off.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

int8_t MMU_$POWER_OFF(void)
{
    uint16_t w = MMU_POWER_REG;
    w ^= 0;
    return (w & 0x0004) ? -1 : 0;
}

#endif /* !ARCH_M68K */
