/*
 * MMU_$REMOVE_LIST - Remove mappings for a list of physical pages
 *
 * Removes virtual-to-physical mappings for an array of physical
 * page numbers. More efficient than calling MMU_$REMOVE repeatedly
 * because it only disables interrupts once.
 *
 * Original address: 0x00e23d92
 */

#include "mmu_internal.h"

void MMU_$REMOVE_LIST(uint32_t *ppn_array, uint16_t count)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    int16_t i;

    if (count == 0) return;

    DISABLE_INTERRUPTS(saved_sr);

    /* Enable PTT access */
    old_csr = MMU_$PID_PRIV;
    MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

    /* Remove each mapping */
    for (i = count - 1; i >= 0; i--) {
        mmu_$remove_internal((uint16_t)ppn_array[i]);
    }

    /* Restore CSR and interrupts */
    MMU_CSR = old_csr;
    ENABLE_INTERRUPTS(saved_sr);
}
