/*
 * MMU_$REMOVE_VIRTUAL - Remove virtual address mappings
 *
 * Scans the PTT looking for mappings that match the given virtual
 * address range and ASID, removes them, and returns the PPNs that
 * were removed.
 *
 * Original address: 0x00e23e38
 */

#include "mmu_internal.h"

void MMU_$REMOVE_VIRTUAL(uint32_t va, uint16_t count, uint16_t asid,
                         uint32_t *ppn_array, uint16_t *removed_count)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    uint16_t *ptt_entry;
    uint32_t match_key;
    int16_t remaining;
    uint32_t *output_ptr;

    if (count == 0) {
        *removed_count = 0;
        return;
    }

    /* Calculate PTT entry address */
    ptt_entry = PTT_FOR_VA(va);

    /* Build the match key from ASID and VA bits */
    /* The key format encodes the ASID and high VA bits for comparison */
    uint32_t va_shifted = va << (*(uint8_t*)((char*)&VA_TO_PTT_OFFSET_MASK + 4) & 0x3F);
    match_key = ((uint32_t)(asid << 9) | ((uint32_t)asid >> 7)) >> 16;

    remaining = count - 1;
    output_ptr = ppn_array;

    do {
        /* Enable PTT access with interrupts disabled */
        DISABLE_INTERRUPTS(saved_sr);
        old_csr = MMU_$PID_PRIV;
        MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

        do {
            uint16_t head_ppn = *ptt_entry & PTT_PPN_MASK;

            if (head_ppn != 0) {
                uint16_t first_ppn = head_ppn;
                uint16_t ppn = head_ppn;
                uint16_t prev_offset = 0;

                /* Search hash chain for matching ASID */
                do {
                    uint32_t *pmape = PFT_FOR_PPN(ppn);
                    uint32_t pmape_val = *pmape;
                    uint16_t pmape_high = (uint16_t)(pmape_val >> 16);

                    /* Check if ASID matches (compare high bits) */
                    if (((pmape_high ^ (uint16_t)match_key) & 0xFE0F) == 0) {
                        /* Match found - remove this entry */
                        /* Update hash chain */
                        uint16_t next_ppn = pmape_val & PFT_LINK_MASK;

                        if (ppn == first_ppn) {
                            /* Removing head of chain */
                            *ptt_entry = next_ppn;
                        } else {
                            /* Removing from middle/end of chain */
                            uint16_t *prev_link = (uint16_t*)((char*)PFT_BASE + prev_offset + 2);
                            uint16_t prev_val = *prev_link;
                            /* Copy link from removed entry, clear head bit */
                            prev_val &= ~PFT_FLAG_HEAD;
                            *prev_link = ((pmape_val ^ prev_val) & 0x8FFF) ^ prev_val;
                        }

                        /* Clear PMAPE, preserving ref/mod bits */
                        *pmape &= 0x6000;

                        /* Record the removed PPN */
                        *output_ptr++ = ppn;
                        break;
                    }

                    prev_offset = ppn << 2;
                    ppn = pmape_val & PFT_LINK_MASK;
                } while (ppn != first_ppn);
            }

            /* Move to next PTT entry (1KB pages) */
            ptt_entry = (uint16_t*)((char*)ptt_entry + 0x400);

            /* Wrap around PTT if needed */
            if ((uint32_t)ptt_entry >= 0x800000) {
                match_key++;
                ptt_entry = PTT_BASE;
            }

        } while ((remaining & 0x1F) != 0 && --remaining >= 0);

        /* Restore CSR and interrupts */
        MMU_CSR = old_csr;
        ENABLE_INTERRUPTS(saved_sr);

    } while (--remaining >= 0);

    *removed_count = (uint16_t)((uint32_t)((char*)output_ptr - (char*)ppn_array) >> 2);
}
