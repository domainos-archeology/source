/*
 * MMU_$INSTALL_ASID - Install/switch to an address space
 *
 * Switches the current address space by updating the ASID
 * in the MMU CSR and flushing the cache.
 *
 * Original address: 0x00e24204
 */

#include "mmu_internal.h"

void MMU_$INSTALL_ASID(uint16_t asid)
{
    /* Update the current ASID */
    PROC1_$AS_ID = asid;

    /* Update MMU_$PID_PRIV with new ASID in high byte */
    MMU_$PID_PRIV = ((uint8_t)asid << 8) | (MMU_$PID_PRIV & 0x00FF);

    /* Write to hardware CSR */
    MMU_CSR = MMU_$PID_PRIV;

    /* Update power register */
    MMU_POWER_REG = (MMU_POWER_REG & 0xFF00) | MMU_POWER_CONTROL_BYTE;

    /* Clear cache (address space changed) */
    CACHE_$CLEAR();
}
