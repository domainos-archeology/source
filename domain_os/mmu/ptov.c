/*
 * MMU_$PTOV - Translate physical page number to virtual address
 *
 * Returns the virtual address that maps to the given physical page.
 * Uses the ASID table and PMAPE to reconstruct the VA.
 *
 * Original address: 0x00e241b8
 */

#include "mmu.h"

uint32_t MMU_$PTOV(uint32_t ppn)
{
    uint32_t *pmape;
    uint32_t pmape_val;
    uint16_t asid_val;
    uint32_t result;

    /* Get PMAPE entry */
    pmape = PMAPE_FOR_PPN(ppn);
    pmape_val = *pmape;

    /* Check if there's a valid mapping */
    if ((pmape_val & PMAPE_LINK_MASK) == 0) {
        /* No mapping */
        return 0;
    }

    /* Get the ASID/VA info */
    asid_val = ASID_FOR_PPN(ppn);

    /* Reconstruct the virtual address
     * The format differs between 68010 and 68020+ */
    result = (pmape_val & 0x000F0000) | asid_val;

    if (M68020 != 0) {
        /* 68020+: shift left by 6 */
        result <<= 6;
    } else {
        /* 68010: shift VA portion left by 2, then shift all left by 4 */
        result = ((result & 0xFFFF0000) | ((result & 0xFFFF) << 2)) << 4;
    }

    return result;
}
