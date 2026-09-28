/*
 * MMU_$INIT - Pick the 68020 or 68010 MMU parameters at boot
 *
 * 0x00E23D38 - 0x00E23D62 (44 bytes, hand-written `MMU_ASM`).  Single
 * caller: OS_$INIT at 0x00E3383E.
 *
 * The m68k build assembles mmu/sau2/init.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 *
 *   0x00E23D3A  lea (-0xe,PC),A5            A5 = 0xE23D2E, the M68020 word
 *   0x00E23D3E  tst.w (A5) / beq            whole-word test of M68020
 *   0x00E23D42  move.l #0x3ffc00,(0x2,A5)   VA_TO_PTT_OFFSET_MASK
 *   0x00E23D4A  move.w #0x1,(0x6,A5)        MMU_$VA_SHIFT
 *   0x00E23D50  move.w #0x6,(0x8,A5)        MMU_$PTT_SHIFT
 *   0x00E23D5A  move.w (-0x4,PC),(0x5a6,A5) 68010: copy this routine's own
 *                                           `rts' (0x4E75 at 0xE23D58) over
 *                                           the first word of CACHE_$CLEAR
 *                                           (0xE242D4), making it a no-op
 * The 68010 path leaves the image-shipped defaults (0x0FFC00, 3, 8) alone.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

/* 0x00E23D58: the `rts' opcode MMU_$INIT copies into CACHE_$CLEAR */
#define MMU_INIT_RTS_OPCODE 0x4E75

void MMU_$INIT(void)
{
    if (M68020_IS_020_W()) {                            /* 0x00E23D3E */
        VA_TO_PTT_OFFSET_MASK = 0x3FFC00;               /* 0x00E23D42 */
        MMU_$VA_SHIFT = 1;                              /* 0x00E23D4A */
        MMU_$PTT_SHIFT = 6;                             /* 0x00E23D50 */
    } else {
        CACHE_$CLEAR_ENTRY = MMU_INIT_RTS_OPCODE;       /* 0x00E23D5A */
    }
}

#endif /* !ARCH_M68K */
