/*
 * MMU_$SET_PROT - Set protection bits for a physical page
 *
 * Updates the protection bits in the PMAPE entry for the given
 * physical page. Returns the previous protection value.
 *
 * Original address: 0x00e2422a
 */

#include "mmu.h"

uint16_t MMU_$SET_PROT(uint32_t ppn, uint16_t prot)
{
    uint16_t saved_sr;
    uint16_t *pmape_word;
    uint16_t old_prot;
    uint16_t new_prot;

    /* Get pointer to PMAPE low word (where protection bits are stored) */
    pmape_word = (uint16_t*)((char*)PFT_BASE + ((ppn & 0xFFFF) << 2));

    /* Disable interrupts for atomic update */
    DISABLE_INTERRUPTS(saved_sr);

    /* Read current protection bits */
    old_prot = *pmape_word & PFT_PROT_MASK;

    /* XOR to update - (old ^ new) clears old and sets new */
    new_prot = prot << PFT_PROT_SHIFT;
    *pmape_word ^= (old_prot ^ new_prot);

    ENABLE_INTERRUPTS(saved_sr);

    /* Return previous protection value (unshifted) */
    return old_prot >> PFT_PROT_SHIFT;
}
