/*
 * MMU_$SET_PROT - Replace a page's protection field, returning the old one
 *
 * 0x00E2422A - 0x00E24258 (48 bytes, hand-written `MMU_ASM`).  Re-emitted
 * 2026-09-27: the earlier C worked on the low word of the PFT entry
 * through a byte-pointer cast; the image's `(0x0,A0,D1w*0x1)` is the
 * FIRST word of the entry, i.e. bits 16..31 of the longword, and the
 * protection field is bits 4..8 of that word.
 *
 * The m68k build assembles mmu/sau2/set_prot.s (byte-checked); this is
 * the host-side model.
 *
 * Arguments: (8,SP) ppn longword (low word << 2 indexes the PFT, sign
 * extended), (0xc,SP) prot word (<< 4 as a word).  Under a saved-SR IPL-7
 * bracket: old = entry_hi & 0x1f0; entry_hi ^= old ^ new; return
 * old >> 4.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

uint16_t (MMU_$SET_PROT)(uint32_t ppn, uint32_t prot_slot)
{
    uint16_t prot = ARCH_PASCAL_SLOT_WORD(prot_slot);   /* (0xC,SP) after one save */
    uint16_t saved_sr;
    uint16_t d3 = (uint16_t)(prot << 4);                         /* 0x00E24230 */
    uint16_t idx4 = (uint16_t)(ppn << 2);                        /* 0x00E24236 */
    uint32_t *e = (uint32_t *)((char *)PFT_BASE + (int16_t)idx4);
    uint16_t old;

    DISABLE_INTERRUPTS(saved_sr);                                /* 0x00E2423E */
    old = (uint16_t)((*e >> 16) & 0x01F0);                       /* 0x00E24244 */
    d3 ^= old;                                                   /* 0x00E2424C */
    *e ^= (uint32_t)d3 << 16;                                    /* 0x00E2424E */
    ENABLE_INTERRUPTS(saved_sr);                                 /* 0x00E24252 */
    return (uint16_t)(old >> 4);                                 /* 0x00E24256 */
}

#endif /* !ARCH_M68K */
