/*
 * MMU_$REMOVE_ASID - Unlink every pageable page owned by an address space
 *
 * 0x00E23F0C - 0x00E23F80 (118 bytes, hand-written `MMU_ASM`).  Re-emitted
 * 2026-09-27.  Wrong before: the critical section was modelled as a
 * save/restore pair; the image raises IPL 7 with a bare `ori #0x700,SR`
 * (0x00E23F42) and leaves with `andi #0xf8ff,SR` (0x00E23F74), which
 * FORCES IPL 0.  The dbeq/dbf counter is now modelled as the word D5.
 *
 * The m68k build assembles mmu/sau2/remove_asid.s (byte-checked); this is
 * the host-side model.
 *
 * Argument: (0x20,SP) asid word -> D7, sign-extended and rotated right 7
 * so it compares directly with a PFT entry's top seven bits (D6 =
 * 0xfe000000).  A3 walks the PFT from MMAP_$LPPN for MMAP_$HPPN -
 * MMAP_$LPPN + 1 entries.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$REMOVE_ASID(uint16_t asid)
{
    uint32_t idx;                   /* A3 as a PFT index */
    uint16_t d5;                    /* D5: dbeq / dbf counter */
    uint32_t d7;                    /* D7: the rotated asid */
    const uint32_t d6 = 0xFE000000u;
    uint32_t v;

    /* 0x00E23F10 - 0x00E23F34 */
    idx = (uint16_t)MMAP_$LPPN;
    d5 = (uint16_t)(MMAP_$HPPN - MMAP_$LPPN);
    d7 = (uint32_t)(int32_t)(int16_t)asid;
    d7 = (d7 >> 7) | (d7 << 25);

    for (;;) {
        /* 0x00E23F36 - 0x00E23F40: dbeq D5 - scan for a match */
        for (;;) {
            v = PFT_FOR_PPN(0)[idx++] & d6;
            if (v == d7) {
                break;
            }
            if (d5 == 0) {
                return;                                 /* bne 0x00E23F40 */
            }
            d5--;
        }
        /* 0x00E23F42 - 0x00E23F52 */
        SET_IPL7();
        MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;
        /* 0x00E23F54 - 0x00E23F6A: re-read the entry under the bracket */
        idx--;
        v = PFT_FOR_PPN(0)[idx] & d6;
        if (v == d7) {
            mmu_$remove_pmape((uint16_t)idx);
        }
        idx++;
        /* 0x00E23F6C - 0x00E23F78 */
        MMU_CSR = MMU_$PID_PRIV;
        SET_IPL0();
        if (d5 == 0) {
            return;                                     /* dbf falls through */
        }
        d5--;
    }
}

#endif /* !ARCH_M68K */
