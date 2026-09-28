/*
 * MMU_$NORMAL_MODE - Is the MMU status register's "normal mode" bit set?
 *
 * 0x00E24280 - 0x00E2428A (12 bytes, hand-written `MMU_ASM`):
 *   btst.b #0x4,(0xFFB403) / sne D0b / rts
 * Verified against the disassembly on 2026-09-22; faithful.  The result
 * is the Domain boolean byte in D0b (0xFF / 0).
 *
 * The m68k build assembles mmu/sau2/normal_mode.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

int8_t MMU_$NORMAL_MODE(void)
{
    return (MMU_STATUS_REG & 0x10) ? -1 : 0;
}

#endif /* !ARCH_M68K */
