/*
 * SIO2681_$XMIT - Load a character into a channel's transmitter
 *
 * Writes the byte to THR and, if the channel's TxRDY bit is not yet in
 * the IMR shadow, sets it there and in the chip's IMR.
 *
 * Original address: 0x00E1D4FC, 66 bytes (SAU2 map: SIO_IO segment)
 *
 *   00e1d4fc    link.w A6,-0x4
 *   00e1d500    movem.l {A2 D2},-(SP)
 *   00e1d504    movea.l (0x8,A6),A0            ; channel
 *   00e1d508    movea.l (0x4,A0),A1            ; chip
 *   00e1d50c    movea.l (A0),A2 ; move.b (0xc,A6),(0x7,A2)   ; THR = ch (byte in a word slot)
 *   00e1d514    clr.w D1w ; clr.w D0w
 *   00e1d518    move.w (0x12,A0),D2w           ; int_bit
 *   00e1d51c    move.b (0x8,A1),D1b            ; imr_shadow
 *   00e1d520    bset.l D2,D0                   ; D0 = 1 << int_bit
 *   00e1d522    move.w D1w,D2w ; and.w D0w,D2w ; bne 0x00e1d534   ; already enabled
 *   00e1d528    or.w D1w,D0w ; move.b D0b,(0x8,A1)   ; imr_shadow |= bit
 *   00e1d52e    movea.l (A1),A2 ; move.b D0b,(0xb,A2) ; IMR = imr_shadow
 *   00e1d534    movem.l (-0xc,A6),{D2 A2} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

void SIO2681_$XMIT(sio2681_channel_t *channel, uint8_t ch)
{
    sio2681_chip_t *chip = channel->chip;
    uint16_t bit;
    uint16_t imr;

    /* 0x00E1D50C-0x00E1D50E */
    channel->regs[SIO2681_REG_THRA] = ch;

    /* 0x00E1D514-0x00E1D526: bset.l numbers bits modulo 32 */
    imr = chip->imr_shadow;
    bit = (uint16_t)(1u << (channel->int_bit & 0x1F));
    if ((imr & bit) == 0) {
        /* 0x00E1D528-0x00E1D530: only the low byte of the word reaches memory */
        imr |= bit;
        chip->imr_shadow = (uint8_t)imr;
        chip->regs[SIO2681_REG_IMR] = (uint8_t)imr;
    }
}
