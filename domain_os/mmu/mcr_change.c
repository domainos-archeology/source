/*
 * MMU_$MCR_CHANGE - Toggle MCR (Memory Control Register) bits
 *
 * Toggles a bit in the Memory Control Register. The bit position
 * differs between 68010 and 68020+ hardware.
 *
 * For 68010: Uses a shadow register that is then combined with
 *            the hardware mask register.
 * For 68020+: Directly modifies the hardware MCR.
 *
 * Original address: 0x00e242a0
 */

#include "mmu.h"

void MMU_$MCR_CHANGE(uint16_t bit)
{
    if (M68020 != 0) {
        /* 68020+: Bit position is inverted (0xB - bit) */
        uint8_t hw_bit = (0x0B - bit) & 7;
        MMU_MCR_M68020 ^= (1 << hw_bit);
    } else {
        /* 68010: Toggle bit in shadow register */
        MCR_SHADOW ^= (1 << (bit & 7));

        /* Combine shadow with hardware mask and write */
        MMU_MCR_M68010 = MCR_SHADOW | (MMU_MCR_MASK & 1);
    }
}
