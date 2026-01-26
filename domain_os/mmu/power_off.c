/*
 * MMU_$POWER_OFF - Check if power-off mode is active
 *
 * Returns non-zero if the system is in power-off or
 * power-saving mode.
 *
 * Original address: 0x00e2428c
 */

#include "mmu_internal.h"

int8_t MMU_$POWER_OFF(void)
{
    uint16_t power_reg;

    /* Read power register */
    power_reg = MMU_POWER_REG;

    /* XOR with 0 is a no-op but matches disassembly */
    power_reg ^= 0;

    /* Check bit 2 - power-off indicator */
    return (power_reg & 0x04) ? -1 : 0;
}
