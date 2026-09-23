/*
 * SIO2681_$TONE - Drive the tone output (OP7) of a channel's chip
 *
 * Bit 7 of the chip's output-port shadow becomes the complement of the
 * caller's enable byte's bit 7, and the shadow is pushed through SOPBC /
 * ROPBC.  Under the driver's spin lock.
 *
 * Original address: 0x00E1D172, 104 bytes (SAU2 map: SIO_IO segment).
 * A5 = 0xE2DEB8 = SIO2681_$DATA.  Three arguments: a cell holding the
 * channel's address, the enable byte's address, and a status cell that is
 * never read (TONE_$ENABLE pushes it; the call reclaims 12 bytes).
 *
 *   00e1d172    link.w A6,-0x10
 *   00e1d176    movem.l {A5 A2 D2},-(SP)
 *   00e1d17a    lea (0xe2deb8).l,A5
 *   00e1d180    movea.l (0x8,A6),A0 ; move.l (A0),D2   ; channel = *channel_cell
 *   00e1d186    pea (A5) ; jsr ML_$SPIN_LOCK ; addq.w #4,SP ; move.w D0w,(-0x6,A6)
 *   00e1d194    movea.l D2,A0 ; movea.l (0xc,A6),A1 ; movea.l (0x4,A0),A0   ; A0 = channel->chip
 *   00e1d19e    move.b (A1),D1b ; not.b D1b
 *   00e1d1a2    andi.b #0x7f,(0x6,A0) ; andi.b #-0x80,D1b ; or.b D1b,(0x6,A0)
 *   00e1d1b0    movea.l (A0),A2 ; move.b (0x6,A0),(0x1d,A2)   ; SOPBC = shadow
 *   00e1d1b8    move.b (0x6,A0),D1b ; eori.b #-0x1,D1b ; move.b D1b,(0x1f,A2)   ; ROPBC = ~shadow
 *   00e1d1c4    subq.l #2,SP ; move.w D0w,-(SP) ; pea (A5) ; jsr ML_$SPIN_UNLOCK
 *   00e1d1d0    movem.l (-0x1c,A6),{D2 A2 A5} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

void SIO2681_$TONE(sio2681_channel_t **channel_cell, uint8_t *enable_ptr,
                   status_$t *status_unused)
{
    sio2681_channel_t *channel;     /* D2 */
    sio2681_chip_t *chip;
    ml_$spin_token_t token;         /* (-0x6,A6) */
    uint8_t opr;

    (void)status_unused;            /* (0x10,A6) is never read */

    /* 0x00E1D180-0x00E1D190 */
    channel = *channel_cell;
    token = ML_$SPIN_LOCK(&SIO2681_$DATA.spin_lock);

    /* 0x00E1D194-0x00E1D1AC */
    chip = channel->chip;
    opr = (uint8_t)((SIO2681_CHIP_OPR(chip) & 0x7F) | ((uint8_t)~*enable_ptr & 0x80));
    SIO2681_CHIP_SET_OPR(chip, opr);

    /* 0x00E1D1B0-0x00E1D1C0 */
    chip->regs[SIO2681_REG_SOPBC] = opr;
    chip->regs[SIO2681_REG_ROPBC] = (uint8_t)(opr ^ 0xFF);

    /* 0x00E1D1C4-0x00E1D1CA */
    ML_$SPIN_UNLOCK(&SIO2681_$DATA.spin_lock, token);
}
