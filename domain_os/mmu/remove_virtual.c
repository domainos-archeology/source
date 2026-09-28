/*
 * MMU_$REMOVE_VIRTUAL - Unlink an address space's pages at a run of VAs
 *
 * 0x00E23E38 - 0x00E23F0A (212 bytes, hand-written `MMU_ASM`).  Re-emitted
 * 2026-09-27.  Wrong before: the head removal was done inline with the
 * PTT pointed at the removed entry's successor (the image hands every
 * match to mmu_$unlink_from_hash, which points the PTT at the
 * PREDECESSOR), the 32-page grouping of the CSR/SR bracket was lost, and
 * the match key was an approximation.
 *
 * The m68k build assembles mmu/sau2/remove_virtual.s (byte-checked); this
 * is the host-side model.
 *
 * Arguments: (0x28,SP) va, (0x2c,SP) count word, (0x2e,SP) asid word,
 * (0x30,SP) ppn_array (a longword per page found), (0x34,SP)
 * removed_count -> word.
 *
 * D4 = ((va << MMU_$VA_SHIFT) with asid in the low word) ror 7, then
 * swapped: its low word is the key compared against each PFT entry's
 * high word under 0xfe0f.  A2 walks the PTT from the entry for va in
 * 0x400 steps, wrapping from 0x800000 to 0x700000 and incrementing D4.
 * D7 = count - 1: the inner `dbeq' processes pages while (D7 & 0x1f) != 0
 * (decrementing D7), the bracket is dropped, and the outer `dbf'
 * decrements D7 again and continues until it wraps.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$REMOVE_VIRTUAL(uint32_t va, uint16_t count, uint16_t asid,
                         uint32_t *ppn_array, uint16_t *removed_count)
{
    uint16_t saved_sr;              /* D6 */
    uint32_t d4;                    /* D4: key in the low word */
    uint32_t ptt_off;               /* A2 - 0x700000 */
    uint16_t d7;                    /* D7 */
    uint32_t *out = ppn_array;      /* A4 */
    uint16_t head, d1, d2;          /* D5, D1, D2 */
    uint32_t d3;                    /* D3 */
    uint16_t *ptt;
    uint32_t *e;

    /* 0x00E23E42 - 0x00E23E6C */
    ptt_off = va & VA_TO_PTT_OFFSET_MASK;
    d4 = va << (MMU_$VA_SHIFT & 0x3F);
    d4 = (d4 & 0xFFFF0000u) | asid;
    d4 = (d4 >> 7) | (d4 << 25);
    d4 = (d4 >> 16) | (d4 << 16);
    d7 = (uint16_t)(count - 1);

    for (;;) {
        /* 0x00E23E6E - 0x00E23E86 */
        DISABLE_INTERRUPTS(saved_sr);
        MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;
        for (;;) {
            /* 0x00E23E88 - 0x00E23EB2: walk the ring at this PTT entry */
            ptt = (uint16_t *)((char *)PTT_BASE + ptt_off);
            head = (uint16_t)(*ptt & 0x0FFF);
            if (head != 0) {
                d1 = 0;
                d2 = head;
                for (;;) {
                    e = (uint32_t *)((char *)PFT_BASE + (int16_t)(uint16_t)(d2 << 2));
                    d3 = *e;
                    if ((((d3 >> 16) ^ d4) & 0xFE0F) == 0) {
                        /* 0x00E23EB4 - 0x00E23EC4 */
                        mmu_$unlink_from_hash(d2, d1, d3, ptt, e);
                        *out++ = d2;
                        break;
                    }
                    d1 = (uint16_t)(d2 << 2);
                    d2 = (uint16_t)(d3 & 0x0FFF);
                    if (d2 == head) {
                        break;
                    }
                }
            }
            /* 0x00E23ECC - 0x00E23EDE: next PTT entry, wrapping */
            ptt_off += 0x400;
            if (ptt_off + 0x700000u >= 0x800000u) {
                d4 += 1;
                ptt_off = 0;
            }
            /* 0x00E23EE0 - 0x00E23EE6: dbeq on (D7 & 0x1f) */
            if ((d7 & 0x1F) == 0) {
                break;
            }
            d7--;
            if (d7 == 0xFFFF) {
                break;
            }
        }
        /* 0x00E23EEA - 0x00E23EF4 */
        MMU_CSR = MMU_$PID_PRIV;
        ENABLE_INTERRUPTS(saved_sr);
        if (d7 == 0) {
            break;
        }
        d7--;
    }

    /* 0x00E23EF8 - 0x00E23F04 */
    *removed_count = (uint16_t)((uint32_t)(out - ppn_array));
}

#endif /* !ARCH_M68K */
