/*
 * SIO2681_$INT - 2681 interrupt service
 *
 * Entered from the SIO2681_$INTn_RTE stubs with the chip's SIO2681_$PTRS
 * entry.  Each pass reads ISR & imr_shadow; a zero ends the routine.
 * Conditions are handled in the order B receive, A receive, A transmit,
 * B transmit, input change, and after each one the routine restarts the
 * pass if that condition was the only one pending, otherwise clears it
 * from the pending byte and goes on.
 *
 * Original address: 0x00E1CEEC, 552 bytes (SAU2 map: "I E1CEEC SIO_IO
 * size = 69C", first routine).  A5 = 0xE2DEB8 = SIO2681_$DATA.
 *
 * Frame: -0x6 pending byte, -0x4 status-register byte, -0x2 ipcr byte
 * (read as the high byte of a word for the btst.l #8.. tests).
 *
 *   00e1ceec    link.w A6,-0x14
 *   00e1cef0    movem.l {A5 A4 A3 A2 D5 D4 D3 D2},-(SP)
 *   00e1cef4    lea (0xe2deb8).l,A5
 *   00e1cefa    D3 = entry ; A4 = entry->chan_a ; A3 = entry->chan_b ; A2 = entry->chip
 *   00e1cf0a    D4 = &chip->imr_shadow ; D5 = &chan_b->sio_desc
 *   --- pass ---
 *   00e1cf18    movea.l (A2),A0 ; move.b (0xb,A0),(-0x6,A6)   ; ISR
 *   00e1cf22    and.b imr_shadow,(-0x6,A6) ; D2 = pending ; beq exit
 *   00e1cf34    btst.b #5 : B receive
 *   00e1cf3c      SR = chan_b->regs[3] ; idx = (SR & 0xF0) >> 4
 *   00e1cf48      SIO_$I_RCV(chan_b->sio_desc, regs[7], error_table[idx])
 *   00e1cf68      cmpi.w #0x20,D2 ; beq pass ; bclr.b #5,(-0x6,A6)
 *   00e1cf74    btst.b #1 : A receive, same shape with chan_a, == 2 -> pass
 *   00e1cfbc    btst.b #0 : A transmit
 *   00e1cfc4      tst.w (0x10,A4) ; bne -> PCHIST_$INTERRUPT(&entry->saved_pc) ;
 *                 SIO2681_$XMIT(chan_a, 0x20)
 *   00e1cfe8      else SIO_$I_XMIT_DONE(chan_a->sio_desc) ; bmi skip ;
 *                 bclr.b #0,imr_shadow ; IMR = imr_shadow
 *   00e1d004      pending == 1 -> pass ; bclr.b #0,(-0x6,A6)
 *   00e1d018    btst.b #4 : B transmit, same with chan_b / bit 4 / == 0x10
 *   00e1d074    tst.b (-0x6,A6) ; bpl pass       ; bit 7: input change
 *   00e1d07c      ipcr = chip->regs[9]
 *   00e1d088      ipcr bit 4 -> SIO_$I_CTS_CHANGE(chan_a->sio_desc, ipcr bit 0 == 0)
 *   00e1d0a8      ipcr bit 5 -> SIO_$I_CTS_CHANGE(chan_b->sio_desc, ipcr bit 1 == 0)
 *   00e1d0c8      ipcr bit 6 -> SIO_$I_DCD_CHANGE(chan_a->sio_desc, ipcr bit 2 == 0)
 *   00e1d0e4      tst.w ; bpl pass ; ipcr bit 7 -> SIO_$I_DCD_CHANGE(chan_b->sio_desc, ipcr bit 3 == 0)
 *   00e1d106    bra pass
 *   00e1d10a    movem.l (-0x34,A6),{D2 D3 D4 D5 A2 A3 A4 A5} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

static uint32_t sio2681_$rcv_error(uint8_t sr)
{
    /* 0x00E1CF44-0x00E1CF52: (SR & 0xF0) >> 4, longword index */
    return SIO2681_$DATA.error_table[(sr & 0xF0) >> 4];
}

void SIO2681_$INT(sio2681_ptrs_entry_t *entry)
{
    sio2681_channel_t *chan_a = entry->chan_a;      /* A4 */
    sio2681_channel_t *chan_b = entry->chan_b;      /* A3 */
    sio2681_chip_t *chip = entry->chip;             /* A2 */
    uint8_t pending;                                /* (-0x6,A6) */
    uint8_t sr;                                     /* (-0x4,A6) */
    uint8_t ipcr;                                   /* (-0x2,A6) */

    for (;;) {
        /* 0x00E1CF18-0x00E1CF30 */
        pending = chip->regs[SIO2681_REG_ISR];
        pending &= chip->imr_shadow;
        if (pending == 0) {
            return;
        }

        /* 0x00E1CF34-0x00E1CF72: channel B receive */
        if ((pending & SIO2681_INT_RXRDY_B) != 0) {
            sr = chan_b->regs[SIO2681_REG_SRA];
            SIO_$I_RCV(chan_b->sio_desc, chan_b->regs[SIO2681_REG_RHRA],
                       sio2681_$rcv_error(sr));
            if (pending == SIO2681_INT_RXRDY_B) {
                continue;
            }
            pending &= (uint8_t)~SIO2681_INT_RXRDY_B;
        }

        /* 0x00E1CF74-0x00E1CFBA: channel A receive */
        if ((pending & SIO2681_INT_RXRDY_A) != 0) {
            sr = chan_a->regs[SIO2681_REG_SRA];
            SIO_$I_RCV(chan_a->sio_desc, chan_a->regs[SIO2681_REG_RHRA],
                       sio2681_$rcv_error(sr));
            if (pending == SIO2681_INT_RXRDY_A) {
                continue;
            }
            pending &= (uint8_t)~SIO2681_INT_RXRDY_A;
        }

        /* 0x00E1CFBC-0x00E1D016: channel A transmit */
        if ((pending & SIO2681_INT_TXRDY_A) != 0) {
            if (chan_a->flags != 0) {
                PCHIST_$INTERRUPT(&entry->saved_pc);
                SIO2681_$XMIT(chan_a, 0x20);
            } else if (SIO_$I_XMIT_DONE(chan_a->sio_desc) >= 0) {
                chip->imr_shadow &= (uint8_t)~SIO2681_INT_TXRDY_A;
                chip->regs[SIO2681_REG_IMR] = chip->imr_shadow;
            }
            if (pending == SIO2681_INT_TXRDY_A) {
                continue;
            }
            pending &= (uint8_t)~SIO2681_INT_TXRDY_A;
        }

        /* 0x00E1D018-0x00E1D072: channel B transmit */
        if ((pending & SIO2681_INT_TXRDY_B) != 0) {
            if (chan_b->flags != 0) {
                PCHIST_$INTERRUPT(&entry->saved_pc);
                SIO2681_$XMIT(chan_b, 0x20);
            } else if (SIO_$I_XMIT_DONE(chan_b->sio_desc) >= 0) {
                chip->imr_shadow &= (uint8_t)~SIO2681_INT_TXRDY_B;
                chip->regs[SIO2681_REG_IMR] = chip->imr_shadow;
            }
            if (pending == SIO2681_INT_TXRDY_B) {
                continue;
            }
            pending &= (uint8_t)~SIO2681_INT_TXRDY_B;
        }

        /* 0x00E1D074-0x00E1D078: bits 2, 3 and 6 fall through to a new pass */
        if ((pending & SIO2681_INT_INPUT_CHANGE) == 0) {
            continue;
        }

        /* 0x00E1D07C-0x00E1D106: input port change */
        ipcr = chip->regs[SIO2681_REG_IPCR];
        if ((ipcr & SIO2681_IPCR_DELTA_CTS_A) != 0) {
            SIO_$I_CTS_CHANGE(chan_a->sio_desc,
                              ((ipcr & SIO2681_IPCR_CTS_A) == 0) ? true : false);
        }
        if ((ipcr & SIO2681_IPCR_DELTA_CTS_B) != 0) {
            SIO_$I_CTS_CHANGE(chan_b->sio_desc,
                              ((ipcr & SIO2681_IPCR_CTS_B) == 0) ? true : false);
        }
        if ((ipcr & SIO2681_IPCR_DELTA_DCD_A) != 0) {
            SIO_$I_DCD_CHANGE(chan_a->sio_desc,
                              ((ipcr & SIO2681_IPCR_DCD_A) == 0) ? true : false);
        }
        if ((ipcr & SIO2681_IPCR_DELTA_DCD_B) != 0) {
            SIO_$I_DCD_CHANGE(chan_b->sio_desc,
                              ((ipcr & SIO2681_IPCR_DCD_B) == 0) ? true : false);
        }
    }
}
