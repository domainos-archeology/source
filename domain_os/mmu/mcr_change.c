/*
 * MMU_$MCR_CHANGE - Toggle one memory-control bit
 *
 * 0x00E242A0 - 0x00E242D2 (50 bytes, hand-written `MMU_ASM`), followed at
 * 0x00E242D2 by the two-byte shadow cell MCR_SHADOW it reaches with
 * `lea (0x30,PC),A0` (image bytes f0 00).  Verified against the
 * disassembly on 2026-09-22; the earlier emission was faithful.
 *
 * Argument: (0x4,SP) bit number, word.
 *
 * 68020 (high byte of M68020 non-zero, `move.b (M68020,PC),D0b`):
 *   bchg.b (0xb - bit),(0xFFB408)      bit number taken modulo 8
 * 68010:
 *   bchg.b bit,(MCR_SHADOW)            modulo 8
 *   (0xFFB405) = ((0xFFB407) & 1) | MCR_SHADOW
 *
 * The m68k build assembles mmu/sau2/mcr_change.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void (MMU_$MCR_CHANGE)(uint32_t bit_slot)
{
    uint16_t bit = ARCH_PASCAL_SLOT_WORD(bit_slot);      /* (4,SP) */
    if (M68020_IS_020_B()) {                                        /* 0x00E242A4 */
        MMU_MCR_M68020 ^= (uint8_t)(1u << ((0x0B - bit) & 7));     /* 0x00E242AA */
    } else {
        MCR_SHADOW ^= (uint8_t)(1u << (bit & 7));                   /* 0x00E242B8 */
        MMU_MCR_M68010 = (uint8_t)((MMU_MCR_MASK & 1) | MCR_SHADOW);/* 0x00E242BE */
    }
}

#endif /* !ARCH_M68K */
