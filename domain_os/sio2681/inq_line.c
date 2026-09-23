/*
 * SIO2681_$INQ_LINE - Report CTS / DCD for a channel
 *
 * Reads the chip's input port (IPR) and, for the selectors present in
 * `mask`, rewrites bit 1 (CTS) and bit 2 (DCD) of params->flags1; then
 * ORs the channel's reserved_14 word into flags1.  The IPR lines are
 * active low: channel A uses IP0 (CTS) / IP2 (DCD), channel B IP1 / IP3.
 * Status is always 0.
 *
 * Original address: 0x00E725B0, 166 bytes (SAU2 map: "I E725B0 SIO_IO
 * size = E0")
 *
 *   00e725b0    link.w A6,-0xc
 *   00e725b4    movem.l {A3 A2 D3 D2},-(SP)
 *   00e725b8    movea.l (0x8,A6),A0            ; channel
 *   00e725bc    movea.l (0xc,A6),A1            ; params
 *   00e725c0    move.l (0x10,A6),D0            ; mask
 *   00e725c4    movea.l (0x14,A6),A2 ; clr.l (A2)   ; *status_ret = 0
 *   00e725ca    andi.l #0x180 -> D1 ; beq exit
 *   00e725d4    movea.l (0x4,A0),A2 ; movea.l (A2),A3
 *   00e725da    move.b (0x1b,A3),(-0x2,A6)     ; ipr (high byte of the word at -0x2)
 *   00e725e0    btst.b #0x1,(0x19,A0)          ; chan_flags & CHAN_FLAG_A
 *   00e725e6    beq.b 0x00e725fc
 *   00e725e8    D2 = seq(ipr bit 0) ; D1 = ipr bit 2   ; channel A
 *   00e725fc    D2 = seq(ipr bit 1) ; D1 = ipr bit 3   ; channel B
 *   00e7260e    seq D1b                        ; dcd asserted (line low)
 *   00e72610    btst.l #0x8,D0 ; beq            ; mask bit 8: CTS
 *   00e72616    tst.b D2b ; bpl -> bclr.b #1,(0x3,A1) ; else bset.b #1,(0x3,A1)
 *   00e72628    tst.b D0b ; bpl exit            ; mask bit 7: DCD
 *   00e7262c    tst.b D1b ; bmi set
 *   00e72630    btst.b #0x6,(0x7,A1) ; beq clear ; params->flags2 bit 6
 *   00e72638    bset.b #2,(0x3,A1) / 00e72640 bclr.b #2,(0x3,A1)
 *   00e72646    move.l (0x14,A0),D3 ; or.l D3,(A1)   ; flags1 |= reserved_14
 *   00e7264c    movem.l (-0x1c,A6),{D2 D3 A2 A3} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

void SIO2681_$INQ_LINE(sio2681_channel_t *channel, sio_params_t *params_ret,
                       uint32_t mask, status_$t *status_ret)
{
    uint8_t ipr;                    /* (-0x2,A6) */
    boolean cts_asserted;           /* D2 */
    boolean dcd_asserted;           /* D1 */

    /* 0x00E725C8 */
    *status_ret = status_$ok;

    /* 0x00E725CA-0x00E725D2 */
    if ((mask & 0x180) == 0) {
        return;
    }

    /* 0x00E725D4-0x00E7260E */
    ipr = channel->chip->regs[SIO2681_REG_IPR];
    if ((channel->chan_flags & SIO2681_CHAN_FLAG_A) != 0) {
        cts_asserted = ((ipr & SIO2681_IPCR_CTS_A) == 0) ? true : false;
        dcd_asserted = ((ipr & SIO2681_IPCR_DCD_A) == 0) ? true : false;
    } else {
        cts_asserted = ((ipr & SIO2681_IPCR_CTS_B) == 0) ? true : false;
        dcd_asserted = ((ipr & SIO2681_IPCR_DCD_B) == 0) ? true : false;
    }

    /* 0x00E72610-0x00E72626 */
    if ((mask & 0x100) != 0) {
        if (cts_asserted < 0) {
            params_ret->flags1 |= 0x02;
        } else {
            params_ret->flags1 &= ~(uint32_t)0x02;
        }
    }

    /* 0x00E72628-0x00E72644: DCD, or the flags2 bit-6 override */
    if ((mask & 0x80) != 0) {
        if (dcd_asserted < 0 || (params_ret->flags2 & 0x40) != 0) {
            params_ret->flags1 |= 0x04;
        } else {
            params_ret->flags1 &= ~(uint32_t)0x04;
        }
    }

    /* 0x00E72646-0x00E7264A */
    params_ret->flags1 |= channel->reserved_14;
}
