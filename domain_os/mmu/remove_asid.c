/*
 * MMU_$REMOVE_ASID - Remove all mappings for an address space
 *
 * Scans all PMAPE entries and removes any that belong to the
 * specified address space ID (ASID). This is used during process
 * termination or address space destruction.
 *
 * Original address: 0x00e23f0c
 */

#include "mmu.h"

/* Forward declaration of internal helper */
extern void mmu_$remove_pmape(uint16_t ppn);

void MMU_$REMOVE_ASID(uint16_t asid)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    uint32_t *pmape;
    int16_t remaining;
    uint32_t asid_match;

    /* Calculate range to scan (pageable pages only) */
    remaining = (int16_t)MMAP_$HPPN - (int16_t)MMAP_$LPPN;

    /* Build ASID match value (shifted for comparison with high word) */
    asid_match = ((uint32_t)(int16_t)asid << 25) | ((uint32_t)(int16_t)asid >> 7);

    /* Start at lowest pageable page */
    pmape = PMAPE_FOR_PPN((uint16_t)MMAP_$LPPN);

    do {
        /* Check if this entry matches the ASID (compare high bits) */
        if ((*pmape & 0xFE000000) != asid_match) {
            /* No match - continue scanning */
            pmape++;
            remaining--;
            if (remaining < 0) {
                return;
            }
            continue;
        }

        /* Found a match - need to remove it with interrupts disabled */
        SET_SR(GET_SR() | SR_IPL_DISABLE_ALL);
        old_csr = MMU_$PID_PRIV;
        MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

        /* Double-check the match (may have changed) */
        if ((*pmape & 0xFE000000) == asid_match) {
            /* Calculate PPN from PMAPE pointer */
            uint16_t ppn = (uint16_t)(((char*)pmape - (char*)MMU_PMAPE_BASE) >> 2);
            mmu_$remove_pmape(ppn);
        }

        /* Restore CSR */
        MMU_CSR = old_csr;

        /* Restore interrupts and continue */
        SET_SR(GET_SR() & ~SR_IPL_DISABLE_ALL);

        pmape++;
        remaining--;
    } while (remaining >= 0);
}
