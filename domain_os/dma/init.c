/*
 * DMA_$INIT - Initialise the four M68450 DMA channels
 *
 * Re-emitted from the image (0x00E0A362..0x00E0A3A4, 68 bytes).  Four calls
 * to DMA_$INIT_M68450_CHANNEL(base, number), each with a Pascal result slot
 * (`subq.l #2,SP`) that the callee never fills, in the order 3, 2, 0, 1:
 *
 *   00e0a368  move.w #0x3 ; pea (0xffa0c0).l      ; channel 3
 *   00e0a378  move.w #0x2 ; pea (0xffa080).l      ; channel 2
 *   00e0a388  clr.w       ; move.l #0xffa000      ; channel 0
 *   00e0a396  move.w #0x1 ; pea (0xffa040).l      ; channel 1
 *
 * The last call's 8 bytes are popped by the `unlk`.  Sole caller
 * 0x00E3291A (OS_$INIT).
 *
 * Original address: 0x00e0a362
 */

#include "dma/dma_internal.h"

void DMA_$INIT(void)
{
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN3_VIRTUAL_ADDRESS, 3);   /* 0x00E0A372 */
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN2_VIRTUAL_ADDRESS, 2);   /* 0x00E0A382 */
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN0_VIRTUAL_ADDRESS, 0);   /* 0x00E0A390 */
    DMA_$INIT_M68450_CHANNEL((uint8_t *)DN300_DMAC_CHAN1_VIRTUAL_ADDRESS, 1);   /* 0x00E0A3A0 */
}
