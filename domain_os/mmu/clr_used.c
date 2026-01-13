/*
 * MMU_$CLR_USED - Clear the referenced bit for a physical page
 *
 * Clears the "used" (referenced) bit in the PMAPE entry.
 * This is used by the page replacement algorithm to track
 * which pages have been accessed.
 *
 * Original address: 0x00e2425a
 */

#include "mmu.h"

void MMU_$CLR_USED(uint32_t ppn)
{
    uint16_t *pmape_word;

    /* Get pointer to PMAPE high word (where ref bit is stored) */
    pmape_word = (uint16_t*)((char*)MMU_PMAPE_BASE + ((ppn & 0xFFFF) << 2) + 2);

    /* Clear the referenced bit */
    *pmape_word &= ~PMAPE_FLAG_REFERENCED;
}
