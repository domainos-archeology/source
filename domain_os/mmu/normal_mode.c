/*
 * MMU_$NORMAL_MODE - Check if MMU is in normal mode
 *
 * Returns non-zero if the MMU is operating in normal mode
 * (as opposed to diagnostic or test mode).
 *
 * Original address: 0x00e24280
 */

#include "mmu.h"

int8_t MMU_$NORMAL_MODE(void)
{
    /* Check bit 4 of status register */
    return (MMU_STATUS_REG & 0x10) ? -1 : 0;
}
