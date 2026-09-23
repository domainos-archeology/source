/*
 * sio2681_set_baud_rate - Program a channel's clock-select register
 *
 * A procedure nested in SIO2681_$SET_LINE: it addresses the driver's
 * tables through the A5 (= SIO2681_$DATA) its caller established and is
 * only reached from there (0x00E1D31A, 0x00E1D34A).  Bit 7 of the chip's
 * ACR shadow selects the baud-rate set; the CSR gets the tx code in its
 * low nibble and the rx code in its high nibble; the channel remembers
 * the tx rate's support bits.
 *
 * Original address: 0x00E1D1DA, 118 bytes (SAU2 map: inside the SIO_IO
 * segment at 0xE1CEEC, no symbol of its own)
 *
 *   00e1d1da    link.w A6,-0xc
 *   00e1d1de    movem.l {A3 A2},-(SP)
 *   00e1d1e2    movea.l (0x8,A6),A0            ; channel
 *   00e1d1e6    move.b (0x10,A6),D0b           ; extended (byte in a word slot)
 *   00e1d1ea    movea.l (0x4,A0),A1            ; chip
 *   00e1d1ee    andi.b #0x7f,(0x4,A1)          ; ACR shadow bit 7 cleared
 *   00e1d1f4    move.b D0b,D1b ; andi.b #-0x80,D1b ; or.b D1b,(0x4,A1)   ; ACR shadow bit 7 = extended bit 7
 *   00e1d1fe    move.w (0xc,A6),D1w ; add.w D1w,D1w ; lea (0x0,A5,D1w*1),A2   ; A2 = A5 + 2*tx
 *   00e1d208    andi.b #-0x10,(-0x6,A6)        ; csr low nibble cleared
 *   00e1d20e    move.b (0x85,A2),D1b ; or.b D1b,(-0x6,A6)   ; |= baud_codes[tx] low byte
 *   00e1d216    move.w (0xe,A6),D1w ; add.w D1w,D1w ; lea (0x0,A5,D1w*1),A3   ; A3 = A5 + 2*rx
 *   00e1d220    andi.b #0xf,(-0x6,A6)          ; csr high nibble cleared
 *   00e1d226    move.b (0x85,A3),D1b ; lsl.b #4,D1b ; or.b D1b,(-0x6,A6)
 *   00e1d230    movea.l (A1),A3 ; move.b (0x4,A1),(0x9,A3)   ; ACR = shadow
 *   00e1d238    movea.l (A0),A3 ; move.b (-0x6,A6),(0x3,A3)  ; CSR = csr
 *   00e1d240    move.w (0x62,A2),(0x1a,A0)     ; baud_support = baud_bits[tx]
 *   00e1d246    movem.l (-0x14,A6),{A2 A3} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

void sio2681_set_baud_rate(sio2681_channel_t *channel,
                           int16_t tx_rate, int16_t rx_rate, int8_t extended)
{
    sio2681_chip_t *chip = channel->chip;
    uint8_t acr;
    uint8_t csr;                    /* (-0x6,A6): never initialised; both
                                     * nibbles are replaced below */

    /* 0x00E1D1EE-0x00E1D1FA */
    acr = (uint8_t)((SIO2681_CHIP_ACR(chip) & 0x7F) | ((uint8_t)extended & 0x80));
    SIO2681_CHIP_SET_ACR(chip, acr);

    /* 0x00E1D208-0x00E1D22C: low byte of the word entries */
    csr = (uint8_t)(SIO2681_$DATA.baud_codes[tx_rate] & 0xFF);
    csr = (uint8_t)((csr & 0x0F) | ((SIO2681_$DATA.baud_codes[rx_rate] & 0xFF) << 4));

    /* 0x00E1D230-0x00E1D23A */
    chip->regs[SIO2681_REG_ACR] = acr;
    channel->regs[SIO2681_REG_CSRA] = csr;

    /* 0x00E1D240 */
    channel->baud_support = SIO2681_$DATA.baud_bits[tx_rate];
}
