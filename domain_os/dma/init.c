/*
 * DMA_$INIT - Initialize all M68450 DMA controller channels
 *
 * Original address: 0x00e0a362
 *
 * This function initializes all four channels of the M68450 DMA controller.
 * It is typically called during system boot to ensure all DMA channels
 * are in a known safe state.
 *
 * The DN300/DN400 has the DMA controller mapped at 0x00FFA000, with
 * each channel occupying 0x40 bytes:
 *   - Channel 0: 0x00FFA000
 *   - Channel 1: 0x00FFA040
 *   - Channel 2: 0x00FFA080
 *   - Channel 3: 0x00FFA0C0
 */

#include "dma/dma_internal.h"

void DMA_$INIT(void)
{
    /*
     * Initialize all four DMA channels.
     *
     * Note: The original code initializes channels in order 3, 2, 0, 1.
     * This may be intentional (perhaps to set up priorities in a specific
     * order) or simply an artifact of the original Pascal code structure.
     * We preserve the original order here.
     */
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN3_VIRTUAL_ADDRESS, 3);
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN2_VIRTUAL_ADDRESS, 2);
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN0_VIRTUAL_ADDRESS, 0);
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN1_VIRTUAL_ADDRESS, 1);
}
