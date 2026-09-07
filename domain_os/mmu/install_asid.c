/*
 * MMU_$INSTALL_ASID - Install/switch to an address space
 *
 * Switches the current address space by updating the ASID
 * in the MMU CSR and flushing the cache.
 *
 * Original address: 0x00e24204
 */

#include "mmu/mmu_internal.h"

void MMU_$INSTALL_ASID(uint16_t asid)
{
    /* Update the current ASID */
    PROC1_$AS_ID = asid;

    /* Update MMU_$PID_PRIV with new ASID in high byte */
    MMU_$PID_PRIV = ((uint8_t)asid << 8) | (MMU_$PID_PRIV & 0x00FF);

    /* Write to hardware CSR */
    MMU_CSR = MMU_$PID_PRIV;

    /*
     * Restore the FP-owner ASID byte into the power register.
     *
     *   00e2421c  move.b (0x00e218d5).l,(0x00ffb402).l
     *
     * 0xE218D5 is the low byte of FP_$OWNER (0xE218D4), and the store is a
     * single byte - it does not read the register back first.
     */
    MMU_POWER_REG_BYTE = (uint8_t)FP_$OWNER;

    /* Clear cache (address space changed) */
    CACHE_$CLEAR();
}
