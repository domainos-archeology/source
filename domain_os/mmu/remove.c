/*
 * MMU_$REMOVE - Remove a mapping for a physical page
 *
 * Removes the virtual-to-physical mapping for the given physical
 * page number (PPN). This updates the PTT and PMAPE hash chain.
 *
 * Original address: 0x00e23d64
 */

#include "mmu.h"

/* Internal helper: remove mapping with interrupts already disabled */
static void mmu_$remove_internal(uint16_t ppn);

void MMU_$REMOVE(uint32_t ppn)
{
    uint16_t saved_sr;
    uint16_t old_csr;

    /* Disable interrupts */
    saved_sr = GET_SR();
    SET_SR(saved_sr | 0x0700);

    /* Enable PTT access */
    old_csr = MMU_$PID_PRIV;
    MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

    /* Remove the mapping */
    mmu_$remove_internal((uint16_t)ppn);

    /* Restore CSR and interrupts */
    MMU_CSR = old_csr;
    SET_SR(saved_sr);
}

/*
 * mmu_$remove_internal - Internal remove helper
 *
 * Removes a PPN from the hash chain and clears its PMAPE entry.
 * Must be called with interrupts disabled and PTT access enabled.
 *
 * Original address: 0x00e23dcc
 */
static void mmu_$remove_internal(uint16_t ppn)
{
    uint32_t *pmape = PMAPE_FOR_PPN(ppn);
    uint16_t asid_entry = ASID_FOR_PPN(ppn);
    uint32_t pmape_val = *pmape;
    uint16_t link = pmape_val & PMAPE_LINK_MASK;

    if (link == 0) {
        /* Not in any hash chain */
        return;
    }

    /* Find the PTT entry for this mapping */
    uint16_t *ptt = (uint16_t*)((uint32_t)PTT_BASE + ((uint32_t)asid_entry << 6));

    uint16_t prev_offset = 0;

    if (link != ppn) {
        /* We're not at the head of the chain - find our predecessor */
        uint16_t curr = link;
        do {
            prev_offset = curr << 2;
            uint16_t next = *(uint16_t*)((char*)MMU_PMAPE_BASE + prev_offset + 2) & PMAPE_LINK_MASK;
            curr = next;
        } while (curr != ppn);

        /* Update predecessor's link to skip us */
        uint16_t *prev_link = (uint16_t*)((char*)MMU_PMAPE_BASE + prev_offset + 2);
        uint16_t prev_val = *prev_link;
        /* Copy our link to predecessor, preserving flags */
        *prev_link = ((pmape_val ^ prev_val) & 0x8FFF) ^ prev_val;
    }

    /* Update PTT entry */
    *ptt = prev_offset >> 2;

    /* Clear PMAPE entry, preserving only reference/modified bits */
    *pmape &= 0x6000;
}
