/*
 * MMU_$SET_SYSREV - Set MMU system revision from hardware
 *
 * Reads the hardware revision register and stores it in the
 * MMU global state. This is called during system initialization.
 *
 * Original address: 0x00e24272
 */

#include "mmu.h"

void MMU_$SET_SYSREV(void)
{
    /* Read hardware revision and store in global */
    MMU_SYSREV = DN330_MMU_HARDWARE_REV;
}
