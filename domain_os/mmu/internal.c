/*
 * MMU Internal Helper Functions
 *
 * These are internal functions used by the public MMU API.
 * They implement the low-level hash chain manipulation.
 *
 * Original addresses:
 * - mmu_$remove_pmape: 0x00e23dd8
 * - mmu_$unlink_from_hash: 0x00e23df4
 */

#include "mmu.h"

/*
 * mmu_$remove_pmape - Remove PMAPE entry and update hash chain
 *
 * Removes a physical page from the hash chain and updates
 * the PTT entry. Must be called with interrupts disabled
 * and PTT access enabled.
 *
 * The ppn parameter should be passed in D2 register, and
 * the PMAPE pointer in A3 register (m68k calling convention).
 *
 * Original address: 0x00e23dd8
 */
void mmu_$remove_pmape(uint16_t ppn)
{
    uint32_t *pmape = PMAPE_FOR_PPN(ppn);
    uint16_t asid_entry = ASID_FOR_PPN(ppn);
    uint32_t pmape_val = *pmape;
    uint16_t link = pmape_val & PMAPE_LINK_MASK;

    if (link == 0) {
        /* Not in any hash chain */
        return;
    }

    /* Calculate PTT entry from ASID info
     * PTT is at base + (asid_entry << 6) */
    uint16_t *ptt = (uint16_t*)((uint32_t)PTT_BASE + ((uint32_t)asid_entry << 6));

    uint16_t prev_offset = 0;

    if (link != ppn) {
        /* We're not at the head - walk the chain to find predecessor */
        uint16_t curr = link;
        do {
            prev_offset = curr << 2;
            uint16_t next = *(uint16_t*)((char*)MMU_PMAPE_BASE + prev_offset + 2) & PMAPE_LINK_MASK;
            curr = next;
        } while (curr != ppn);

        /* Update predecessor's link to skip us */
        uint16_t *prev_link = (uint16_t*)((char*)MMU_PMAPE_BASE + prev_offset + 2);
        uint16_t prev_val = *prev_link;
        /* Copy our link to predecessor, clear head bit, preserve other flags */
        prev_val &= ~PMAPE_FLAG_HEAD;
        *prev_link = ((pmape_val ^ prev_val) & 0x8FFF) ^ prev_val;
    }

    /* Update PTT entry to point to predecessor (or 0 if we were head) */
    *ptt = prev_offset >> 2;

    /* Clear PMAPE entry, preserving only reference/modified bits */
    *pmape &= 0x6000;
}

/*
 * mmu_$unlink_from_hash - Unlink entry from hash chain
 *
 * Removes an entry from the middle of a hash chain, updating
 * the previous entry's link and the PTT entry as needed.
 *
 * Original address: 0x00e23df4
 */
void mmu_$unlink_from_hash(uint16_t ppn, uint16_t prev_offset,
                           uint32_t pmape_val, uint16_t *ptt_entry,
                           uint32_t *pmape)
{
    uint16_t link = pmape_val & PMAPE_LINK_MASK;

    if (link == 0) {
        /* Entry not in chain */
        return;
    }

    if (link != ppn) {
        /* Not at head of chain - need to update predecessor */
        if (prev_offset == 0) {
            /* Find the predecessor by walking the chain */
            uint16_t curr = link;
            do {
                prev_offset = curr << 2;
                uint16_t next = *(uint16_t*)((char*)MMU_PMAPE_BASE + prev_offset + 2) & PMAPE_LINK_MASK;
                curr = next;
            } while (curr != ppn);
        }

        /* Update predecessor's link */
        uint16_t *prev_link = (uint16_t*)((char*)MMU_PMAPE_BASE + prev_offset + 2);
        uint16_t prev_val = *prev_link;
        /* Clear head bit, copy link from removed entry */
        prev_val &= ~PMAPE_FLAG_HEAD;
        *prev_link = (((pmape_val & 0xFFFF) ^ prev_val) & 0x8FFF) ^ prev_val;
    }

    /* Update PTT entry */
    *ptt_entry = prev_offset >> 2;

    /* Clear PMAPE, preserving ref/mod bits */
    *pmape &= 0x6000;
}
