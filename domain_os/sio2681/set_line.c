/*
 * SIO2681_$SET_LINE - Program a channel's speed, format and modem outputs
 *
 * Under the driver's spin lock: quiesces the channel (disable, reset RX,
 * reset TX), then for selector bits 0|1 programs the baud rate, for
 * 0x41C rebuilds MR1/MR2 from the templates, for 0x60 rewrites the
 * chip's output-port shadow (RTS/DTR) and pushes it through SOPBC/ROPBC,
 * and finally issues reset-error + enable.  The only failure is
 * status_$sio_incompatible_speed (0x360008) when the requested tx rate
 * has no support bits or, unless selector bit 1 is set, is not in the
 * peer channel's current set.
 *
 * Original address: 0x00E1D250, 676 bytes (SAU2 map: SIO_IO segment at
 * 0xE1CEEC).  A5 = 0xE2DEB8 = SIO2681_$DATA; sio2681_set_baud_rate is
 * nested here and uses that A5.
 *
 * Frame: -0x4 status, -0x8 baud_rate copy (tx word at -0x8, rx at -0x6),
 *        -0xA lock token, -0xC MR2 word (high byte edited), -0xE MR1 word.
 *
 *   00e1d250    link.w A6,-0x1c
 *   00e1d254    movem.l {A5 A4 A3 A2 D5 D4 D3 D2},-(SP)
 *   00e1d258    lea (0xe2deb8).l,A5
 *   00e1d25e    D3 = channel ; D4 = params ; D2 = change_mask ; clr.l (-0x4,A6)
 *   00e1d26e    ML_$SPIN_LOCK(A5) -> (-0xa,A6)
 *   00e1d27c    A1 = channel->regs ; CR = (0x5c) 0x0A ; CR = (0x58) 0x2A ; CR = (0x5a) 0x3A
 *   00e1d292    bclr.b #0,(0x19,A0)            ; chan_flags bit 0
 *   00e1d298    moveq #3 ; and.l D2 ; beq 0x00e1d352
 *   00e1d2a2    move.l (0xc,A2),(-0x8,A6)      ; baud_rate
 *   00e1d2a8    moveq #0x10 ; cmp.w (-0x6) ; bcs skip ; cmp.w (-0x8) ; bcs skip   ; both <= 0x10 or silently skip
 *   00e1d2ba    D1 = tx ; A4 = channel->peer ; A3 = A5 + 2*tx
 *   00e1d2c8    tst.w (0x62,A3) ; beq invalid   ; baud_bits[tx] == 0
 *   00e1d2ce    btst.l #1,D2 ; bne ok            ; selector bit 1 skips the peer check
 *   00e1d2d4    baud_bits[tx] & peer->baud_support ; bne ok
 *   00e1d2de    move.l #0x360008,(-0x4,A6) ; bra 0x00e1d352
 *   00e1d2e8    A2 = channel->chip
 *   00e1d2f0    tst.w (0x4,A2) ; smi D1 ; D5 = D1>>7 ; D1 = baud_mask[D5] & baud_bits[tx] ; sne D1
 *   00e1d308    tst.w (0x4,A2) ; smi D5 ; cmp.b D5,D1 ; seq D1   ; ext = (rate in current set) == current ext
 *   00e1d312    sio2681_set_baud_rate(channel, tx, rx, ext)   [pushes: byte D1, long (-0x8), D3]
 *   00e1d322    tst.w (0x4,A2) ; smi D1 ; D0 = D1>>7 ; peer->baud_support & baud_mask[D0] ; bne 0x00e1d352
 *   00e1d33a    sio2681_set_baud_rate(peer, default_baud hi, lo, smi(config1))
 *   00e1d352    andi.l #0x41c,D1 ; beq 0x00e1d450
 *   00e1d35e    move.w (0x60,A5),(-0xe,A6)     ; MR1 template 0x0B00
 *   00e1d366    params->parity: 3 -> &= 0xE3 ; 1 -> &= 0xE7, |= 0x04 ; 0 -> &= 0xE7, |= 0x10 ; else untouched
 *   00e1d39e    params->char_size: >= 4 -> untouched ; 0 -> &= 0xFC ; 1 -> &= 0xFC, |= 1 ; 2 -> &= 0xFC, |= 2 ; 3 -> |= 3
 *   00e1d3e4    move.w (0x5e,A5),(-0xc,A6)     ; MR2 template 0x0700
 *   00e1d3ea    btst.b #1,(0x7,A1) -> 0/1 ; mr2 = (mr2 & 0xEF) | bit<<4   ; flags2 bit 1 = CTS control
 *   00e1d400    params->stop_bits: 1 -> &= 0xF0, |= 7 ; 2 -> &= 0xF0, |= 8 ; 3 -> |= 0xF ; else untouched
 *   00e1d43a    CR = (0x54) 0x10 ; MR = mr1 ; MR = mr2
 *   00e1d450    moveq #0x60 ; and.l D2 ; beq 0x00e1d4d2
 *   00e1d456    A0 = channel->chip ; btst.b #1,(0x19,A1)   ; chan_flags bit 1 (channel A)
 *   00e1d466    A: opr bit 2 = flags1 bit 3 ; opr bit 0 = flags1 bit 0
 *   00e1d490    B: opr bit 3 = flags1 bit 3 ; opr bit 1 = flags1 bit 0
 *   00e1d4be    SOPBC = opr ; ROPBC = ~opr
 *   00e1d4d2    CR = (0x56) 0x45
 *   00e1d4dc    ML_$SPIN_UNLOCK(A5, token)
 *   00e1d4ea    *status_ret = (-0x4,A6)
 *   00e1d4f2    movem.l (-0x3c,A6),{D2 D3 D4 D5 A2 A3 A4 A5} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

/* 0x00E1D2F0 / 0x00E1D308 / 0x00E1D322 / 0x00E1D33C: `tst.w (0x4,A2); smi` -
 * the ACR shadow's bit 7 as a Domain boolean. */
static boolean sio2681_$extended_set(const sio2681_chip_t *chip)
{
    return ((int16_t)chip->config1 < 0) ? true : false;
}

/* 0x00E1D2FE / 0x00E1D334: baud_mask_a for the standard set, baud_mask_b
 * for the extended one (`(0x50,A5,D5w*1)` with D5 = 0 or 2). */
static uint16_t sio2681_$set_mask(boolean extended)
{
    return (extended < 0) ? SIO2681_$DATA.baud_mask_b : SIO2681_$DATA.baud_mask_a;
}

void SIO2681_$SET_LINE(sio2681_channel_t *channel, sio_params_t *params,
                       uint32_t change_mask, status_$t *status_ret)
{
    status_$t status = status_$ok;  /* (-0x4,A6) */
    ml_$spin_token_t token;         /* (-0xA,A6) */
    volatile uint8_t *regs;
    sio2681_channel_t *peer;
    sio2681_chip_t *chip;
    uint32_t baud;                  /* (-0x8,A6) */
    uint16_t tx_rate, rx_rate;
    uint16_t bits;
    boolean in_current_set;
    boolean extended;
    uint8_t mr1, mr2;               /* high bytes of (-0xE,A6) / (-0xC,A6) */
    uint8_t opr;
    uint8_t bit;

    /* 0x00E1D26E-0x00E1D278 */
    token = ML_$SPIN_LOCK(&SIO2681_$DATA.spin_lock);

    /* 0x00E1D27C-0x00E1D296 */
    regs = channel->regs;
    regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_disable_rx_tx;
    regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_rx;
    regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_tx;
    channel->chan_flags &= (uint16_t)~SIO2681_CHAN_FLAG_BIT0;

    /* 0x00E1D298-0x00E1D34E: speed */
    if ((change_mask & 0x03) != 0) {
        baud = params->baud_rate;
        tx_rate = (uint16_t)(baud >> 16);
        rx_rate = (uint16_t)(baud & 0xFFFF);
        /* an index above 0x10 silently skips the whole arm */
        if (rx_rate <= 0x10 && tx_rate <= 0x10) {
            peer = channel->peer;
            bits = SIO2681_$DATA.baud_bits[tx_rate];
            if (bits == 0 ||
                ((change_mask & 0x02) == 0 && (bits & peer->baud_support) == 0)) {
                status = status_$sio_incompatible_speed;
            } else {
                chip = channel->chip;
                /* 0x00E1D2F0-0x00E1D310 */
                in_current_set = ((sio2681_$set_mask(sio2681_$extended_set(chip)) & bits) != 0)
                                     ? true : false;
                extended = (in_current_set == sio2681_$extended_set(chip)) ? true : false;
                sio2681_set_baud_rate(channel, (int16_t)tx_rate, (int16_t)rx_rate, extended);

                /* 0x00E1D322-0x00E1D34E: the peer must still be in the set
                 * just selected, else it is reprogrammed at the default */
                if ((peer->baud_support & sio2681_$set_mask(sio2681_$extended_set(chip))) == 0) {
                    sio2681_set_baud_rate(peer,
                                          (int16_t)(SIO2681_$DATA.default_baud >> 16),
                                          (int16_t)(SIO2681_$DATA.default_baud & 0xFFFF),
                                          sio2681_$extended_set(chip));
                }
            }
        }
    }

    /* 0x00E1D352-0x00E1D44E: character format */
    if ((change_mask & 0x41C) != 0) {
        mr1 = (uint8_t)(SIO2681_$DATA.mr1_template >> 8);
        switch (params->parity) {
        case 3:  mr1 &= 0xE3; break;
        case 1:  mr1 &= 0xE7; mr1 |= 0x04; break;
        case 0:  mr1 &= 0xE7; mr1 |= 0x10; break;
        default: break;
        }
        if ((uint16_t)params->char_size < 4) {
            switch (params->char_size) {
            case 0:  mr1 &= 0xFC; break;
            case 1:  mr1 &= 0xFC; mr1 |= 0x01; break;
            case 2:  mr1 &= 0xFC; mr1 |= 0x02; break;
            default: mr1 |= 0x03; break;
            }
        }

        mr2 = (uint8_t)(SIO2681_$DATA.mr2_template >> 8);
        bit = ((params->flags2 & 0x02) != 0) ? 1 : 0;
        mr2 = (uint8_t)((mr2 & 0xEF) | (bit << 4));
        switch (params->stop_bits) {
        case 1:  mr2 &= 0xF0; mr2 |= 0x07; break;
        case 2:  mr2 &= 0xF0; mr2 |= 0x08; break;
        case 3:  mr2 |= 0x0F; break;
        default: break;
        }

        /* 0x00E1D43A-0x00E1D44A */
        regs = channel->regs;
        regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_mr_ptr;
        regs[SIO2681_REG_MRA] = mr1;
        regs[SIO2681_REG_MRA] = mr2;
    }

    /* 0x00E1D450-0x00E1D4CE: RTS / DTR through the output port */
    if ((change_mask & 0x60) != 0) {
        chip = channel->chip;
        opr = SIO2681_CHIP_OPR(chip);
        if ((channel->chan_flags & SIO2681_CHAN_FLAG_A) != 0) {
            bit = ((params->flags1 & 0x08) != 0) ? 1 : 0;
            opr = (uint8_t)((opr & 0xFB) | (bit << 2));
            bit = ((params->flags1 & 0x01) != 0) ? 1 : 0;
            opr = (uint8_t)((opr & 0xFE) | bit);
        } else {
            bit = ((params->flags1 & 0x08) != 0) ? 1 : 0;
            opr = (uint8_t)((opr & 0xF7) | (bit << 3));
            bit = ((params->flags1 & 0x01) != 0) ? 1 : 0;
            opr = (uint8_t)((opr & 0xFD) | (bit << 1));
        }
        SIO2681_CHIP_SET_OPR(chip, opr);
        chip->regs[SIO2681_REG_SOPBC] = opr;
        chip->regs[SIO2681_REG_ROPBC] = (uint8_t)(opr ^ 0xFF);
    }

    /* 0x00E1D4D2-0x00E1D4EE */
    channel->regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_err_enable;
    ML_$SPIN_UNLOCK(&SIO2681_$DATA.spin_lock, token);
    *status_ret = status;
}
