/*
 * DMA_$INIT_M68450_CHANNEL - Initialize a single M68450 DMA channel
 *
 * Original address: 0x00e0a328
 *
 * This function initializes a single channel of the M68450 DMA controller
 * to a known safe state. It is called by DMA_$INIT for each of the four
 * channels.
 *
 * Initialization sequence:
 *   1. Assert software abort (CCR = 0x10) - stops any in-progress transfer
 *   2. Clear all status bits (CSR = 0xFF) - acknowledges any pending conditions
 *   3. Set device control (DCR = 0x28) - 68000 compatible mode, burst transfer
 *   4. Set sequence control (SCR = 0x04) - memory address count enabled
 *   5. Set channel priority (CPR = channel_number) - lower number = higher priority
 */

#include "dma/dma_internal.h"

void DMA_$INIT_M68450_CHANNEL(uint8_t *chan_virtual_address, int16_t channel_number)
{
    /*
     * Assert software abort to ensure the channel is stopped.
     * CCR bit 4 (SAB) = Software Abort
     */
    chan_virtual_address[M68450_REG_CCR] = M68450_CCR_SAB;  /* 0x10 */

    /*
     * Clear all status bits by writing 0xFF to CSR.
     * Writing 1s to status bits clears them.
     */
    chan_virtual_address[M68450_REG_CSR] = 0xFF;

    /*
     * Set Device Control Register:
     *   0x28 = 0b00101000
     *   - XRM[7:6] = 00: Burst transfer mode
     *   - DTYP[5:4] = 10: 68000 compatible mode
     *   - DPS[3] = 1: 16-bit device port size
     *   - PCL[1:0] = 00: Status input with interrupt
     */
    chan_virtual_address[M68450_REG_DCR] = 0x28;

    /*
     * Set Sequence Control Register:
     *   0x04 = 0b00000100
     *   - MAC[3:2] = 01: Memory address count up
     *   - DAC[1:0] = 00: Device address held (no count)
     */
    chan_virtual_address[M68450_REG_SCR] = 0x04;

    /*
     * Set Channel Priority Register to the channel number.
     * Lower values = higher priority.
     * Channel 0 has highest priority (0), Channel 3 has lowest (3).
     */
    chan_virtual_address[M68450_REG_CPR] = (uint8_t)channel_number;
}
