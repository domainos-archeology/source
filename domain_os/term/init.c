/*
 * TERM_$INIT - Initialise the terminal subsystem
 *
 * Clears the four DTTEs' handler pointers, builds the console line (display
 * keyboard through the SIO6509 path) and the two SIO2681 serial lines out
 * of the records inside TERM_$DATA, stamps the caller's per-process slot,
 * initialises both chips, optionally arms the ESC crash key on the caller's
 * line, sets TERM_$MAX_DTTE to 3 and starts SUMA.
 *
 * Parameters (frame 0x00E32EC8 / 0x00E32ECE):
 *   0x08 mode_ptr - word by reference (D3): 1 = the caller is process 1's
 *                   console owner (stamp its slot, arm the crash key)
 *   0x0C line_ptr - word by reference (D4): that caller's line number
 *
 * Original address: 0x00e32eba, 868 bytes
 *
 * A5 = 0xE35154, the second OS_TERM_INIT data block (map: D E35154 size 5C):
 * (A5) DAT_00e35154 console vtable, (0x28,A5) DAT_00e3517c serial vtable,
 * (0x4c,A5) DAT_00e351a0 SIO2681 config, (0x5a,A5) DAT_00e351ae SIO6509 config.
 * B = 0xE2C9F0 = TERM_$DATA throughout.
 *
 * Frame cells (all hold 32-bit VAs and are passed BY ADDRESS):
 *   -0x04 0xFFFFFFFF   -0x08 B+0x1158   -0x0C line record   -0x10 B+0x1168
 *   -0x14 SIO desc     -0x18 dtte       -0x1C B+0x1068      -0x20 B+0x10E0
 *   -0x34 / -0x38 scratch cells, reused per call
 *
 *   00e32ecc  (-0x4) = -1
 *   00e32ed6..00e32f10  dbf #3: clr.l dtte[i]+0x24, +0x2C, +0x28, +0x30
 *   00e32f14..00e32f5e  A3 = B+0xFF0 (-0x14); A3' = B+0x1168; A2 = B+0x158;
 *                       A4 = B+0x1158 (-0x8); A0 = B+0x12A0 dtte[0] (-0x18); (-0xC) = A2
 *   00e32f62  OS_TERM_INIT(B+0x1168, dtte[0], &(-0xC), B+0xC0, &(-0x14), B+0xB0)  ; 0x18 popped
 *   00e32f84  SIO_$INIT_LINE(B+0x158, dtte[0], &(-0x8), B+0x70)                    ; 0x10
 *   00e32fa0  (-0xC) = B+0x158; (-0x34) = B+0x52A
 *             SIO_$INIT_DRAIN_HANDLER(B+0x1158, dtte[0], &(-0x34), &(-0xC))         ; 0x10
 *   00e32fc2  (-0x10) = B+0x1168; (-0x34) = B+0x1250; (-0x38) = B+0x1206
 *             SIO_$INIT_DESC(B+0xFF0, B+0x58, dtte[0], &(-0x10), &(-0x38), B+0x88,
 *                            &(-0x34), 0xE35154)                                   ; 0x20
 *   00e33008  SIO_$INIT_DTTE(dtte[0], 2)                            ; result slot, 0x8
 *   00e33018  SIO6509_$INIT(0xE33220, 0xE3321E, B+0x1250, &(-0x14), 0xE351AE)      ; 0x14
 *             ; pea (0x1f8,PC) = 0xE33026+0x1F8 = 0xE3321E, pea (0x1f6,PC) = 0xE33220
 *   00e33036  A4 = B+0x1068 (-0x14); A2 = B+0x634; A0 = dtte[1] (-0x18)
 *   00e33066  SIO_$INIT_LINE(B+0x634, dtte[1], &(-0x14), B+0x40)
 *   00e33080  (-0xC) = B+0x634; (-0x38) = B+0x1268; (-0x34) = B+0xA06
 *             SIO_$INIT_DESC(B+0x1068, B, dtte[1], &(-0xC), &(-0x34), B+0x18, &(-0x38), 0xE3517C)
 *   00e330c2  SIO_$INIT_DTTE(dtte[1], 0)
 *   00e330d0  A3 = B+0x10E0 (-0x14); A2 = B+0xB10; A0 = dtte[2] (-0x18)
 *   00e33106  SIO_$INIT_LINE(B+0xB10, dtte[2], &(-0x14), B+0x40)
 *   00e33120  (-0xC) = B+0xB10; (-0x34) = B+0x1284; (-0x38) = B+0xEE2
 *             SIO_$INIT_DESC(B+0x10E0, B, dtte[2], &(-0xC), &(-0x38), B+0x18, &(-0x34), 0xE3517C)
 *   00e33162  SIO_$INIT_DTTE(dtte[2], 0)
 *   00e33170  if *mode == 1: long at B + 0x1048 + *line*0x78 = -1
 *   00e33196  (-0x20) = B+0x10E0; (-0x1C) = B+0x1068
 *             SIO2681_$INIT(0xE33220, 0xE33220, B+0x1268, &(-0x1C), B+0x10B4,
 *                           B+0x1284, &(-0x20), B+0x112C, B+0x1258, 0xE351A0)      ; 0x28
 *             ; pea (0x5a,PC) = 0xE331C6+0x5A = 0xE33220, then move.l (SP),-(SP)
 *   00e331d4  if *mode == 1: A2 = dtte[*line].handler_ptr (0x12C4 + line*0x38);
 *             TTY_$I_ENABLE_CRASH_FUNC(A2, 0x1B, true)   ; st / move.w #0x1b00
 *   00e33202  TERM_$MAX_DTTE = 3
 *   00e3320e  jsr SUMA_$INIT
 */

#include "term/term_internal.h"

void TERM_$INIT(short *mode_ptr, short *line_ptr)
{
    uint32_t minus_one;             /* A6-0x04 */
    m68k_ptr_t cell_1158;           /* A6-0x08 */
    m68k_ptr_t cell_line;           /* A6-0x0C */
    m68k_ptr_t cell_1168;           /* A6-0x10 */
    m68k_ptr_t cell_desc;           /* A6-0x14 */
    m68k_ptr_t cell_dtte;           /* A6-0x18 */
    m68k_ptr_t cell_1068;           /* A6-0x1C */
    m68k_ptr_t cell_10e0;           /* A6-0x20 */
    m68k_ptr_t cell_34;             /* A6-0x34 */
    m68k_ptr_t cell_38;             /* A6-0x38 */
    int16_t count;                  /* D0w */
    int i;
    int16_t slot_offset;            /* D1w */
    tty_desc_t *crash_tty;          /* A2 at 0x00E331EE */

    /* 0x00E32ECC..0x00E32ED2 */
    minus_one = 0xFFFFFFFFu;

    /* 0x00E32ED6..0x00E32F10: moveq #3 / dbf = 4 entries */
    for (count = 3, i = 0; count >= 0; count--, i++) {
        TERM_$DATA.dtte[i].handler_ptr = 0;     /* +0x24 */
        TERM_$DATA.dtte[i].alt_handler = 0;     /* +0x2C */
        TERM_$DATA.dtte[i].tty_handler = 0;     /* +0x28 */
        TERM_$DATA.dtte[i].ptr_30 = 0;          /* +0x30 */
    }

    /* ---- console line ---------------------------------------------- */

    /* 0x00E32F14..0x00E32F5E */
    cell_desc = ARCH_PTR_TO_VA(DAT_00e2d9e0);           /* B+0xFF0 */
    cell_1158 = ARCH_PTR_TO_VA(DAT_00e2db48);           /* B+0x1158 */
    cell_dtte = ARCH_PTR_TO_VA(&TERM_$DATA.dtte[0]);    /* B+0x12A0 */
    cell_line = ARCH_PTR_TO_VA(DAT_00e2cb48);           /* B+0x158 */

    /* 0x00E32F62..0x00E32F80 */
    OS_TERM_INIT((uint32_t *)DAT_00e2db58, (uint32_t *)&TERM_$DATA.dtte[0],
                 (uint32_t *)&cell_line, (uint32_t *)&PTR_TTY_$I_RCV_00e2cab0,
                 (uint32_t *)&cell_desc, (uint32_t *)DAT_00e2caa0);

    /* 0x00E32F84..0x00E32F9C */
    SIO_$INIT_LINE(DAT_00e2cb48, &TERM_$DATA.dtte[0], &cell_1158, DAT_00e2ca60);

    /* 0x00E32FA0..0x00E32FBE */
    cell_line = ARCH_PTR_TO_VA(DAT_00e2cb48);
    cell_34 = ARCH_PTR_TO_VA(DAT_00e2cf1a);             /* B+0x158+0x3D2 */
    SIO_$INIT_DRAIN_HANDLER((m68k_ptr_t *)DAT_00e2db48, &TERM_$DATA.dtte[0],
                            &cell_34, &cell_line);

    /* 0x00E32FC2..0x00E33004 */
    cell_1168 = ARCH_PTR_TO_VA(DAT_00e2db58);
    cell_34 = ARCH_PTR_TO_VA(DAT_00e2dc40);             /* B+0x1250 */
    cell_38 = ARCH_PTR_TO_VA(DAT_00e2dbf6);             /* B+0x1168+0x9E */
    SIO_$INIT_DESC((sio_desc_t *)DAT_00e2d9e0, DAT_00e2ca48, &TERM_$DATA.dtte[0],
                   &cell_1168, &cell_38, (m68k_ptr_t *)&PTR_KBD_$RCV_00e2ca78,
                   &cell_34, (char *)DAT_00e35154);

    /* 0x00E33008..0x00E33016 */
    SIO_$INIT_DTTE(&TERM_$DATA.dtte[0], 2);

    /* 0x00E33018..0x00E33032 */
    SIO6509_$INIT(&DAT_00e33220, &DAT_00e3321e, DAT_00e2dc40, &cell_desc,
                  DAT_00e351ae);

    /* ---- serial line 1 --------------------------------------------- */

    /* 0x00E33036..0x00E33062 */
    cell_desc = ARCH_PTR_TO_VA(DAT_00e2da58);           /* B+0x1068 */
    cell_dtte = ARCH_PTR_TO_VA(&TERM_$DATA.dtte[1]);

    /* 0x00E33066..0x00E3307C */
    SIO_$INIT_LINE(DAT_00e2d024, &TERM_$DATA.dtte[1], &cell_desc, DAT_00e2ca30);

    /* 0x00E33080..0x00E330BE */
    cell_line = ARCH_PTR_TO_VA(DAT_00e2d024);           /* B+0x634 */
    cell_38 = ARCH_PTR_TO_VA(&TONE_$CHANNEL);           /* B+0x1268 */
    cell_34 = ARCH_PTR_TO_VA(DAT_00e2d3f6);             /* B+0x634+0x3D2 */
    SIO_$INIT_DESC((sio_desc_t *)DAT_00e2da58, DAT_00e2c9f0, &TERM_$DATA.dtte[1],
                   &cell_line, &cell_34, (m68k_ptr_t *)&PTR_TTY_$I_RCV_00e2ca08,
                   &cell_38, (char *)DAT_00e3517c);

    /* 0x00E330C2..0x00E330CE */
    SIO_$INIT_DTTE(&TERM_$DATA.dtte[1], 0);

    /* ---- serial line 2 --------------------------------------------- */

    /* 0x00E330D0..0x00E33102 */
    cell_desc = ARCH_PTR_TO_VA(DAT_00e2dad0);           /* B+0x10E0 */
    cell_dtte = ARCH_PTR_TO_VA(&TERM_$DATA.dtte[2]);

    /* 0x00E33106..0x00E3311C */
    SIO_$INIT_LINE(DAT_00e2d500, &TERM_$DATA.dtte[2], &cell_desc, DAT_00e2ca30);

    /* 0x00E33120..0x00E3315E */
    cell_line = ARCH_PTR_TO_VA(DAT_00e2d500);           /* B+0xB10 */
    cell_34 = ARCH_PTR_TO_VA(DAT_00e2dc74);             /* B+0x1284 */
    cell_38 = ARCH_PTR_TO_VA(DAT_00e2d8d2);             /* B+0xB10+0x3D2 */
    SIO_$INIT_DESC((sio_desc_t *)DAT_00e2dad0, DAT_00e2c9f0, &TERM_$DATA.dtte[2],
                   &cell_line, &cell_38, (m68k_ptr_t *)&PTR_TTY_$I_RCV_00e2ca08,
                   &cell_34, (char *)DAT_00e3517c);

    /* 0x00E33162..0x00E3316E */
    SIO_$INIT_DTTE(&TERM_$DATA.dtte[2], 0);

    /* 0x00E33170..0x00E33190: *line * 0x78 as a sign-extended word */
    if (*mode_ptr == 1) {
        slot_offset = (int16_t)(*line_ptr * 0x78);
        *(uint32_t *)(DAT_00e2da38 + slot_offset) = minus_one;
    }

    /* 0x00E33196..0x00E331D0 */
    cell_10e0 = ARCH_PTR_TO_VA(DAT_00e2dad0);
    cell_1068 = ARCH_PTR_TO_VA(DAT_00e2da58);
    SIO2681_$INIT(&DAT_00e33220, &DAT_00e33220,
                  &TONE_$CHANNEL, (sio_desc_t **)&cell_1068,
                  (sio_params_t *)DAT_00e2daa4,
                  (sio2681_channel_t *)DAT_00e2dc74, (sio_desc_t **)&cell_10e0,
                  (sio_params_t *)DAT_00e2db1c,
                  (sio2681_chip_t *)DAT_00e2dc48, DAT_00e351a0);

    /* 0x00E331D4..0x00E33200: dtte[*line].handler_ptr, a 32-bit VA */
    if (*mode_ptr == 1) {
        slot_offset = (int16_t)(*line_ptr * sizeof(dtte_t));
        crash_tty = (tty_desc_t *)ARCH_VA_TO_PTR(
            ((dtte_t *)((uint8_t *)DTTE + slot_offset))->handler_ptr);
        TTY_$I_ENABLE_CRASH_FUNC(crash_tty, 0x1B, true);
    }

    /* 0x00E33202..0x00E33208 */
    TERM_$MAX_DTTE = 3;

    /* 0x00E3320E */
    SUMA_$INIT();

    (void)cell_dtte;    /* the -0x18 cell is only ever read back as a value */
}
