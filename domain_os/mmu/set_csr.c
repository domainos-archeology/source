/*
 * MMU_$SET_CSR - Set CSR privilege bits
 *
 * Updates the MMU Control/Status Register privilege bits.
 * This is used to change the current privilege level.
 *
 * Original address: 0x00e241f4
 */

#include "mmu_internal.h"

void MMU_$SET_CSR(uint16_t csr_val)
{
    /* Update the high byte of MMU_$PID_PRIV with the new CSR value */
    MMU_$PID_PRIV = (csr_val & 0xFF00) | (MMU_$PID_PRIV & 0x00FF);

    /* Write to hardware CSR */
    MMU_CSR = MMU_$PID_PRIV;
}
