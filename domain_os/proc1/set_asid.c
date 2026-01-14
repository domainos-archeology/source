/*
 * PROC1_$SET_ASID - Set address space ID for current process
 * Original address: 0x00e148f8
 *
 * Sets the ASID (Address Space ID) for the current process and
 * installs it into the MMU. This is used when switching between
 * different address spaces.
 *
 * Parameters:
 *   asid - Address space ID to set
 */

#include "proc1.h"

void PROC1_$SET_ASID(uint16_t asid)
{
    /* Store ASID in current PCB */
    PROC1_$CURRENT_PCB->asid = asid;

    /* Install ASID in MMU hardware */
    MMU_$INSTALL_ASID(asid);
}
