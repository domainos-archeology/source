/*
 * MMU hash-chain helpers (hand-written `MMU_ASM` register routines)
 *
 * Three fall-through entry points share one body in the image:
 *
 *   0x00E23DCC  mmu_$remove_internal   D2 = ppn; sets A3 = PFT entry, then
 *   0x00E23DD8  mmu_$remove_pmape      A2 = PTT entry from MMU_$PTTX,
 *                                      D3 = *A3, D1 = 0 (no known
 *                                      predecessor), then
 *   0x00E23DF4  mmu_$unlink_from_hash  D2 = ppn, D1 = predecessor offset or
 *                                      0, D3 = the entry's value, A2 = PTT
 *                                      entry, A3 = PFT entry
 *
 * Re-emitted from the disassembly on 2026-09-22.  Wrong before: the
 * predecessor fix-up XORed into a copy with bit 15 already cleared (the
 * image XORs the masked delta into the ORIGINAL word, 0x00E23E28, so the
 * head bit of the predecessor ends up as pred15 ^ val15), and the PFT low
 * words were reached through byte-pointer casts.
 *
 * mmu_$remove_internal is in mmu/remove.c next to MMU_$REMOVE; the two
 * inner entries are here.
 *
 * The m68k build assembles mmu/sau2/internal.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

/*
 * 0x00E23DD8 - 0x00E23DF2 then falls into mmu_$unlink_from_hash.
 *   D0 = MMU_$PTTX.entry[ppn] (word) << 6, A2 = PTT_BASE + D0, D3 = *A3, D1 = 0.
 */
void mmu_$remove_pmape(uint16_t ppn)
{
    uint32_t *pft_entry = PFT_FOR_PPN(ppn);
    uint32_t off = (uint32_t)PTTX_FOR_PPN(ppn) << 6;
    uint16_t *ptt_entry = (uint16_t *)((char *)PTT_BASE + off);

    mmu_$unlink_from_hash(ppn, 0, *pft_entry, ptt_entry, pft_entry);
}

/*
 * 0x00E23DF4 - 0x00E23E36.
 */
void mmu_$unlink_from_hash(uint16_t ppn, uint16_t prev_offset,
                           uint32_t pmape_val, uint16_t *ptt_entry,
                           uint32_t *pmape)
{
    uint16_t link = (uint16_t)(pmape_val & 0x0FFF);     /* 0x00E23DF4 */
    uint32_t *pred;
    uint16_t delta;

    /* 0x00E23DFA: an unlinked entry - nothing at all is written */
    if (link == 0) {
        return;
    }

    /* 0x00E23DFC: ppn is its own successor (single-entry chain) - the
     * predecessor fix-up is skipped and D1 (0) becomes the PTT value */
    if (link != ppn) {
        /* 0x00E23E06 - 0x00E23E18: find the predecessor unless the caller
         * supplied one; the walk starts from ppn's own successor */
        if (prev_offset == 0) {
            uint16_t cur = link;
            do {
                prev_offset = (uint16_t)(cur << 2);
                cur = (uint16_t)(PFT_FOR_PPN(0)[prev_offset >> 2] & 0x0FFF);
            } while (cur != ppn);
        }
        /* 0x00E23E1A - 0x00E23E28: low word of the predecessor */
        pred = &PFT_FOR_PPN(0)[prev_offset >> 2];
        delta = (uint16_t)(*pred & 0xFFFF);
        delta &= (uint16_t)~0x8000;                         /* bclr #15 */
        delta ^= (uint16_t)(pmape_val & 0xFFFF);
        delta &= 0x8FFF;
        *pred ^= delta;
    }

    /* 0x00E23E2C - 0x00E23E30 */
    *ptt_entry = (uint16_t)(prev_offset >> 2);
    /* 0x00E23E30: only the referenced/modified bits survive */
    *pmape &= 0x6000;
}

#endif /* !ARCH_M68K */
