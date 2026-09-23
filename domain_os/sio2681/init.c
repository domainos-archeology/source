/*
 * SIO2681_$INIT - Initialise one 2681 DUART and its two channels
 *
 * Original address: 0x00E333DC, 304 bytes (SAU2 map: "I E333DC SIO_IO
 * size = 194").  A5 = 0xE351EC = SIO2681_$INT_VECTORS (the two stub
 * addresses; map "D E351EC SIO_IO size = 8").
 *
 * Arguments (0x8,A6).. : int_vec_ptr, chip_num_ptr, chan_a, chan_a_desc_cell,
 * chan_a_params, chan_b, chan_b_desc_cell, chan_b_params, chip, config.
 *
 *   00e333dc    link.w A6,-0x8
 *   00e333e0    movem.l {A5 A4 A3 A2},-(SP)
 *   00e333e4    lea (0xe351ec).l,A5
 *   00e333ea    movea.l (0xc,A6),A2            ; chip_num_ptr
 *   00e333ee    movea.l (0x2c,A6),A0           ; config
 *   00e333f2    move.w (A2),D0w ; lsl.w #5 ; lea (-0x20,A1,D0w*1),A1  ; regs = 0xFFB000 + (n-1)*0x20
 *   00e33400    movea.l (0x28,A6),A3           ; chip
 *   00e33404    move.l A1,(A3)                 ; chip->regs
 *   00e33406    move.w (A0),(0x4,A3)           ; chip->config1 = config[0]
 *   00e3340a    move.w (0x4,A0),(0x6,A3)       ; chip->config2 = config[2]
 *   00e33410    move.b #-0x5e,(0x8,A3)         ; imr_shadow = 0xA2
 *   00e33416    movea.l (A3),A1 ; move.b #0,(0xb,A1)   ; IMR = 0
 *   00e3341e    move.w (A2),D0w ; lsl.w #4       ; n*0x10 off 0xE2DF80 (1-based)
 *   00e33428    move.l A3,(-0x8,A4,D0w*1)      ; PTRS[n-1].chip
 *   00e3342c    move.l (0x10,A6),(-0x10,A4,D0w*1) ; PTRS[n-1].chan_a
 *   00e33432    A4 = chan_a: regs, sio_desc = *cell, chan_flags = 2, int_bit = 0,
 *               peer = chan_b, chip, flags = 0, reserved_14 = 0, baud_support = 0
 *   00e33462    move.b #0x5,(0x5,A0)           ; CRA = enable RX + TX
 *   00e3346e    move.l (0x1c,A6),(-0xc,A3,D0w*1) ; PTRS[n-1].chan_b
 *   00e33474    A4 = chan_b: regs = base+0x10, sio_desc = *cell, chan_flags = 0,
 *               int_bit = 4, peer = chan_a, chip, flags = 0, reserved_14 = 0,
 *               baud_support = 0
 *   00e334aa    move.b #0x5,(0x5,A3)           ; CRB = enable RX + TX
 *   00e334b0    SIO2681_$SET_LINE(chan_a, chan_a_params, 0x3FFF, &status)
 *   00e334ca    SIO2681_$SET_LINE(chan_b, chan_b_params, 0x3FFF, &status)  (no pop)
 *   00e334de    move.w (A2),D0w ; lsl.w #2 ; move.w (*int_vec_ptr),D1w ; lsl.w #2
 *   00e334f0    move.l (-0x4,A5,D0w*1),(-0x4,A1,D1w*1)   ; vector 0x64-4+vec*4 = stub[n-1]
 *   00e334f6    movea.l (0x28,A6),A3 ; movea.l (A3),A0
 *   00e334fc    move.b (0x8,A3),(0xb,A0)       ; IMR = imr_shadow
 *   00e33502    movem.l (-0x18,A6),{A2 A3 A4 A5} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

void SIO2681_$INIT(int16_t *int_vec_ptr, int16_t *chip_num_ptr,
                   sio2681_channel_t *chan_a_struct, sio_desc_t **chan_a_callback,
                   sio_params_t *chan_a_params,
                   sio2681_channel_t *chan_b_struct, sio_desc_t **chan_b_callback,
                   sio_params_t *chan_b_params,
                   sio2681_chip_t *chip_struct, uint16_t *config)
{
    volatile uint8_t *base_addr;
    int16_t chip_num;
    sio2681_ptrs_entry_t *ptrs;
    status_$t status;               /* (-0x4,A6) */
    int16_t vec_num;
    m68k_ptr_t *vector_table;

    /* 0x00E333F2-0x00E333FC: chip numbers are 1-based (TERM_$INIT passes 1) */
    chip_num = *chip_num_ptr;
    base_addr = (volatile uint8_t *)ARCH_VA_TO_PTR(SIO2681_BASE_ADDR - 0x20 +
                                                   ((uint16_t)chip_num << 5));

    /* 0x00E33404-0x00E33418 */
    chip_struct->regs = base_addr;
    chip_struct->config1 = config[0];
    chip_struct->config2 = config[2];
    chip_struct->imr_shadow = 0xA2;
    base_addr[SIO2681_REG_IMR] = 0;

    /* 0x00E3341E-0x00E3342C: PTRS is 1-based on chip_num */
    ptrs = &SIO2681_$PTRS[chip_num - 1];
    ptrs->chip = chip_struct;
    ptrs->chan_a = chan_a_struct;

    /* 0x00E33432-0x00E33462 */
    chan_a_struct->regs = base_addr;
    chan_a_struct->sio_desc = *chan_a_callback;
    chan_a_struct->chan_flags = SIO2681_CHAN_FLAG_A;
    chan_a_struct->int_bit = 0;
    chan_a_struct->peer = chan_b_struct;
    chan_a_struct->chip = chip_struct;
    chan_a_struct->flags = 0;
    chan_a_struct->reserved_14 = 0;
    chan_a_struct->baud_support = 0;
    chan_a_struct->regs[SIO2681_REG_CRA] = SIO2681_CR_RX_ENABLE | SIO2681_CR_TX_ENABLE;

    /* 0x00E3346E */
    ptrs->chan_b = chan_b_struct;

    /* 0x00E33474-0x00E334AA */
    chan_b_struct->regs = base_addr + 0x10;
    chan_b_struct->sio_desc = *chan_b_callback;
    chan_b_struct->chan_flags = 0;
    chan_b_struct->int_bit = 4;
    chan_b_struct->peer = chan_a_struct;
    chan_b_struct->chip = chip_struct;
    chan_b_struct->flags = 0;
    chan_b_struct->reserved_14 = 0;
    chan_b_struct->baud_support = 0;
    chan_b_struct->regs[SIO2681_REG_CRA] = SIO2681_CR_RX_ENABLE | SIO2681_CR_TX_ENABLE;

    /* 0x00E334B0-0x00E334D8: all selectors, the status is never examined */
    SIO2681_$SET_LINE(chan_a_struct, chan_a_params, 0x3FFF, &status);
    SIO2681_$SET_LINE(chan_b_struct, chan_b_params, 0x3FFF, &status);

    /*
     * 0x00E334DE-0x00E334F0: vector table entry (0x64 - 4 + vec*4), i.e.
     * vector 24+vec, gets stub chip_num (both indices 1-based).
     */
    vec_num = *int_vec_ptr;
    vector_table = (m68k_ptr_t *)ARCH_VA_TO_PTR(0x64);
    vector_table[vec_num - 1] =
        ARCH_PTR_TO_VA((const void *)SIO2681_$INT_VECTORS[chip_num - 1]);

    /* 0x00E334F6-0x00E334FC */
    chip_struct->regs[SIO2681_REG_IMR] = chip_struct->imr_shadow;
}
