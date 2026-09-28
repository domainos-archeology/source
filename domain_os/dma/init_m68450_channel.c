/*
 * DMA_$INIT_M68450_CHANNEL - Put one M68450 channel into its idle state
 *
 * Re-emitted from the image (0x00E0A328..0x00E0A360, 58 bytes) and
 * verified; the previous body was faithful.
 *
 * Frame (link.w A6,-0x4): (0x8,A6) channel base -> A0 (reloaded before
 * every store), (0xC,A6) channel number word -> D0.
 *
 *   00e0a334  move.b #0x10,(0x7,A0)   ; CCR  = SAB (software abort)
 *   00e0a33e  move.b #-0x1,(A0)       ; CSR  = 0xFF (clear every status bit)
 *   00e0a346  move.b #0x28,(0x4,A0)   ; DCR  = 0x28
 *   00e0a350  move.b #0x4,(0x6,A0)    ; SCR  = 0x04
 *   00e0a35a  move.b D0b,(0x2d,A0)    ; CPR  = channel number (low byte)
 *
 * Callers: DMA_$INIT 0x00E0A372 / 0x00E0A382 / 0x00E0A390 / 0x00E0A3A0.
 *
 * Original address: 0x00e0a328
 */

#include "dma/dma_internal.h"

void DMA_$INIT_M68450_CHANNEL(uint8_t *chan_virtual_address, int16_t channel_number)
{
    chan_virtual_address[M68450_REG_CCR] = M68450_CCR_SAB;               /* 0x00E0A334 */
    chan_virtual_address[M68450_REG_CSR] = 0xFF;                         /* 0x00E0A33E */
    chan_virtual_address[M68450_REG_DCR] = 0x28;                         /* 0x00E0A346 */
    chan_virtual_address[M68450_REG_SCR] = 0x04;                         /* 0x00E0A350 */
    chan_virtual_address[M68450_REG_CPR] = (uint8_t)channel_number;      /* 0x00E0A35A */
}
