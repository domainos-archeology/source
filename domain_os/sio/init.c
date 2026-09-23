/*
 * SIO_$INIT - Initialise one serial port's terminal/SIO structures
 *
 * Every address handed to the helpers is carved out of TERM_$DATA
 * (0xE2C9F0, "D E2C9F0 OS_TERM_INIT size = 1398") by port number:
 *
 *   port * 0x78  + 0xF78    the port's sio_desc_t
 *   port * 0x4DC - 0x384    the port's tty descriptor (line data)
 *   port * 0x4DC + 0x4E     the port's transmit buffer
 *   port * 0xE4  + 0x1084   console: terminal state; + 0x1122 its txbuf
 *   port * 0x0C  + 0x114C   console: the 3-longword drain-handler record
 *   max_dtte * 0x38 + 0x12A0  the next free DTTE
 *
 * Port 1 is the console (OS_TERM_INIT, KBD handlers at +0x88, param block
 * at +0x58, hardware block at +0x70, discipline 2); any other port is a
 * plain serial line (TTY handlers at +0x18, param block at +0, hardware
 * block at +0x40, discipline 0) which then either gets the ESC crash
 * character enabled (flags true) or has the driver's set_params applied
 * with every selector (0x3FFF).
 *
 * Original address: 0x00E32BE0, 730 bytes (SAU2 map: OS_TERM segment)
 *
 *   00e32be0    link.w A6,-0xc
 *   00e32be4    movem.l {A4 A3 A2 D6 D5 D4 D3 D2},-(SP)
 *   00e32be8    move.w (0x8,A6),D2w            ; port_num
 *   00e32bec    move.l (0xa,A6),D5             ; context_ptr
 *   00e32bf0    move.l (0xe,A6),D6             ; vtable_ptr
 *   00e32bf4    move.b (0x16,A6),D3b           ; flags (byte in a word slot)
 *   00e32bf8    movea.l (0x18,A6),A4           ; status_ret
 *   00e32bfc    clr.l (A4)
 *   00e32bfe    cmpi.w #0x1,D2w
 *   00e32c02    bne.w 0x00e32d8e
 *   --- console ---
 *   00e32c06    movea.l #0xe2c9f0,A0
 *   00e32c0c    pea (0xb0,A0)                  ; arg6 base+0xB0
 *   00e32c10    move.w D2w,D0w / ext.l / lsl.l #3 / neg / lsl.l #4 / add   ; port*0x78 (32-bit)
 *   00e32c1e    lea (0x0,A0,D0*0x1),A1 ; move.l A1,D4
 *   00e32c24    lea (0xf78,A1),A2 ; move.l A2,(-0x8,A6)
 *   00e32c2c    pea (-0x8,A6)                  ; arg5 &cell(desc)
 *   00e32c30    pea (0xc0,A0)                  ; arg4 base+0xC0
 *   00e32c34    move.w D2w,D3w ; muls.w #0x4dc,D3      ; port*0x4DC (32-bit)
 *   00e32c3a    lea (0x0,A0,D3*0x1),A2 ; lea (-0x384,A2),A3 ; move.l A3,(-0xc,A6)
 *   00e32c46    pea (-0xc,A6)                  ; arg3 &cell(line data)
 *   00e32c4a    move.w (0x1388,A0),D0w / *0x38 (16-bit) / lea / pea (0x12a0,A0) ; arg2 DTTE[max]
 *   00e32c60    move.w D2w,D1w ; muls.w #0xe4,D1 ; lea (0x0,A4,D1*0x1),A3
 *   00e32c70    pea (0x1084,A3)                ; arg1 console terminal state
 *   00e32c74    bsr.w 0x00e32a60               ; OS_TERM_INIT
 *   00e32c78    lea (0x18,SP),SP
 *   00e32c7c    pea (0x70,A4)                  ; arg4 hw block
 *   00e32c80    port*12 (32-bit) -> A4 = base + port*12
 *   00e32c90    lea (0x114c,A4),A0 ; move.l A0,(-0xc,A6)
 *   00e32c98    pea (-0xc,A6)                  ; arg3 &cell(drain record)
 *   00e32c9c    [max_dtte*0x38, as 0x00E32C4A-0x00E32C58] pea (0x12a0,A1)            ; arg2 DTTE[max]
 *   00e32cb8    pea (-0x384,A2)                ; arg1 line data
 *   00e32cbc    bsr.w 0x00e32b26               ; SIO_$INIT_LINE
 *   00e32cc0    lea (0x10,SP),SP
 *   00e32cc4    lea (-0x384,A2),A1 ; move.l A1,(-0xc,A6)
 *   00e32ccc    pea (-0xc,A6)                  ; arg4 &cell(line data)
 *   00e32cd0    lea (0x4e,A0,D3*0x1),A0 ; move.l A0,(-0x8,A6)
 *   00e32cde    pea (-0x8,A6)                  ; arg3 &cell(txbuf)
 *   00e32ce2    [max_dtte*0x38, as 0x00E32C4A-0x00E32C58] pea (0x12a0,A0)            ; arg2 DTTE[max]
 *   00e32cfe    pea (0x114c,A4)                ; arg1 drain record
 *   00e32d02    bsr.w 0x00e32bb8               ; SIO_$INIT_DRAIN_HANDLER
 *   00e32d06    lea (0x10,SP),SP
 *   00e32d0a    move.l D6,-(SP)                ; arg8 vtable_ptr
 *   00e32d0c    move.l D5,-(SP)                ; arg7 context_ptr
 *   00e32d14    pea (0x88,A0)                  ; arg6 KBD handlers
 *   00e32d18    lea (0x1122,A3),A1 ; move.l A1,(-0x8,A6) ; pea (-0x8,A6)  ; arg5 &cell(console txbuf)
 *   00e32d24    lea (0x1084,A3),A0 ; move.l A0,(-0xc,A6) ; pea (-0xc,A6)  ; arg4 &cell(console state)
 *   00e32d30    [max_dtte*0x38, as 0x00E32C4A-0x00E32C58] pea (0x12a0,A2)            ; arg3 DTTE[max]
 *   00e32d52    pea (0x58,A4)                  ; arg2 console param block
 *   00e32d56    movea.l D4,A4 ; pea (0xf78,A4) ; arg1 desc
 *   00e32d5c    bsr.w 0x00e32ab2               ; SIO_$INIT_DESC
 *   00e32d60    lea (0x20,SP),SP
 *   00e32d64    subq.l #0x2,SP ; move.w #0x2,-(SP) ; [max_dtte*0x38, as 0x00E32C4A-0x00E32C58] pea (0x12a0,A1)
 *   00e32d86    bsr.w 0x00e32b76               ; SIO_$INIT_DTTE(DTTE[max], 2)
 *   00e32d8a    bra.w 0x00e32e8a
 *   --- serial line ---
 *   00e32d8e    movea.l #0xe2c9f0,A0
 *   00e32d94    pea (0x40,A0)                  ; arg4 hw block
 *   00e32d98    port*0x78 (32-bit) -> A3 = base + port*0x78
 *   00e32daa    lea (0xf78,A3),A1 ; move.l A1,(-0xc,A6) ; pea (-0xc,A6)  ; arg3 &cell(desc)
 *   00e32db6    [max_dtte*0x38, as 0x00E32C4A-0x00E32C58] pea (0x12a0,A2)            ; arg2 DTTE[max]
 *   00e32dcc    move.w D2w,D4w ; muls.w #0x4dc,D4 ; lea (0x0,A0,D4*0x1),A2
 *   00e32dd6    pea (-0x384,A2)                ; arg1 line data
 *   00e32dda    bsr.w 0x00e32b26               ; SIO_$INIT_LINE
 *   00e32dde    lea (0x10,SP),SP
 *   00e32de2    move.l D6,-(SP) ; move.l D5,-(SP)   ; arg8, arg7
 *   00e32dec    pea (0x18,A0)                  ; arg6 TTY handlers
 *   00e32df0    lea (0x4e,A0,D4*0x1),A1 ; move.l A1,(-0xc,A6) ; pea (-0xc,A6)  ; arg5 &cell(txbuf)
 *   00e32dfc    lea (-0x384,A2),A0 ; move.l A0,(-0x8,A6) ; pea (-0x8,A6)      ; arg4 &cell(line data)
 *   00e32e08    [max_dtte*0x38, as 0x00E32C4A-0x00E32C58] pea (0x12a0,A0)            ; arg3 DTTE[max]
 *   00e32e24    move.l #0xe2c9f0,-(SP)         ; arg2 param block = base
 *   00e32e2a    pea (0xf78,A3)                 ; arg1 desc
 *   00e32e2e    bsr.w 0x00e32ab2               ; SIO_$INIT_DESC
 *   00e32e32    lea (0x20,SP),SP
 *   00e32e36    subq.l #0x2,SP ; clr.w -(SP) ; [max_dtte*0x38, as 0x00E32C4A-0x00E32C58] pea (0x12a0,A1)
 *   00e32e56    bsr.w 0x00e32b76               ; SIO_$INIT_DTTE(DTTE[max], 0)
 *   00e32e5a    addq.w #0x8,SP
 *   00e32e5c    tst.b D3b
 *   00e32e5e    bpl.b 0x00e32e74
 *   00e32e60    st -(SP)                       ; arg3 true
 *   00e32e62    move.w #0x1b00,-(SP)           ; arg2 byte 0x1B
 *   00e32e66    pea (-0x384,A2)                ; arg1 line data
 *   00e32e6a    jsr 0x00e67292.l               ; TTY_$I_ENABLE_CRASH_FUNC
 *   00e32e70    clr.l (A4)                     ; *status_ret = 0
 *   00e32e72    bra.b 0x00e32e8a
 *   00e32e74    movea.l A3,A2
 *   00e32e76    pea (A4)                       ; arg4 status_ret
 *   00e32e78    pea (0x3fff).w                 ; arg3 0x3FFF by value
 *   00e32e7c    pea (0xfc4,A2)                 ; arg2 &desc->params
 *   00e32e80    move.l (0xf78,A2),-(SP)        ; arg1 desc->context
 *   00e32e84    movea.l (0xfb8,A2),A1          ; desc->set_params
 *   00e32e88    jsr (A1)
 *   --- common ---
 *   00e32e8a    port*0x78 (32-bit) ; lea (0xf78,A2),A2
 *   00e32ea6    movea.l (0x12,A6),A3 ; move.l A2,(A3)   ; *desc_ret = desc
 *   00e32eac    addq.w #0x1,(0x1388,A1)        ; TERM_$MAX_DTTE++
 *   00e32eb0    movem.l (-0x2c,A6),{D2 D3 D4 D5 D6 A2 A3 A4}
 *   00e32eb6    unlk A6
 *   00e32eb8    rts
 */

#include "sio/sio_internal.h"

/* base + (port * stride) + offset, with the 32-bit product the image forms */
static uint8_t *sio_$term_addr(int32_t port_product, int32_t offset)
{
    return (uint8_t *)&TERM_$DATA + port_product + offset;
}

void SIO_$INIT(int16_t port_num, void *context_ptr, void *vtable_ptr,
               sio_desc_t **desc_ret, int8_t flags, status_$t *status_ret)
{
    m68k_ptr_t cell_8;      /* (-0x8,A6): a VA passed by reference */
    m68k_ptr_t cell_c;      /* (-0xC,A6): a VA passed by reference */
    int32_t desc_off = (int32_t)port_num * SIO_DESC_STRIDE;       /* port*0x78 */
    int32_t line_off = (int32_t)port_num * SIO_LINE_DATA_STRIDE;  /* port*0x4DC */
    dtte_t *dtte;
    sio_desc_t *desc;

    /* 0x00E32BFC */
    *status_ret = status_$ok;

    /* 0x00E32BFE-0x00E32C02 */
    if (port_num == 1) {
        int32_t console_off = (int32_t)port_num * SIO_CONSOLE_PORT_STRIDE;  /* port*0xE4 */
        int32_t drain_off = (int32_t)port_num * SIO_DRAIN_HANDLER_STRIDE;   /* port*12 */

        /* 0x00E32C06-0x00E32C78 */
        cell_8 = ARCH_PTR_TO_VA(sio_$term_addr(desc_off, SIO_DESC_BASE_OFFSET));
        cell_c = ARCH_PTR_TO_VA(sio_$term_addr(line_off, -SIO_LINE_DATA_ADJUST));
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        OS_TERM_INIT((uint32_t *)(void *)sio_$term_addr(console_off, SIO_CONSOLE_TERM_OFFSET),
                     (uint32_t *)(void *)dtte,
                     &cell_c,
                     (uint32_t *)(void *)sio_$term_addr(0, SIO_CONSOLE_I_RCV_OFFSET),
                     &cell_8,
                     (uint32_t *)(void *)sio_$term_addr(0, SIO_CONSOLE_VTABLE_OFFSET));

        /* 0x00E32C7C-0x00E32CC0 */
        cell_c = ARCH_PTR_TO_VA(sio_$term_addr(drain_off, SIO_DRAIN_HANDLER_OFFSET));
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_LINE(sio_$term_addr(line_off, -SIO_LINE_DATA_ADJUST),
                       dtte,
                       &cell_c,
                       sio_$term_addr(0, SIO_CONSOLE_HW_INFO_OFFSET));

        /* 0x00E32CC4-0x00E32D06 */
        cell_c = ARCH_PTR_TO_VA(sio_$term_addr(line_off, -SIO_LINE_DATA_ADJUST));
        cell_8 = ARCH_PTR_TO_VA(sio_$term_addr(line_off, SIO_LINE_TXBUF_OFFSET));
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_DRAIN_HANDLER((m68k_ptr_t *)(void *)sio_$term_addr(drain_off, SIO_DRAIN_HANDLER_OFFSET),
                                dtte,
                                &cell_8,
                                &cell_c);

        /* 0x00E32D0A-0x00E32D60 */
        cell_8 = ARCH_PTR_TO_VA(sio_$term_addr(console_off, SIO_CONSOLE_TXBUF_OFFSET));
        cell_c = ARCH_PTR_TO_VA(sio_$term_addr(console_off, SIO_CONSOLE_TERM_OFFSET));
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_DESC((sio_desc_t *)(void *)sio_$term_addr(desc_off, SIO_DESC_BASE_OFFSET),
                       sio_$term_addr(0, SIO_CONSOLE_PARAM_OFFSET),
                       dtte,
                       &cell_c,
                       &cell_8,
                       (m68k_ptr_t *)(void *)sio_$term_addr(0, SIO_CONSOLE_HANDLER_OFFSET),
                       (m68k_ptr_t *)context_ptr,
                       (char *)vtable_ptr);

        /* 0x00E32D64-0x00E32D86 (a spare word slot precedes the pushes) */
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_DTTE(dtte, 2);
    } else {
        /* 0x00E32D8E-0x00E32DDE */
        cell_c = ARCH_PTR_TO_VA(sio_$term_addr(desc_off, SIO_DESC_BASE_OFFSET));
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_LINE(sio_$term_addr(line_off, -SIO_LINE_DATA_ADJUST),
                       dtte,
                       &cell_c,
                       sio_$term_addr(0, SIO_GENERIC_HW_INFO_OFFSET));

        /* 0x00E32DE2-0x00E32E32 */
        cell_c = ARCH_PTR_TO_VA(sio_$term_addr(line_off, SIO_LINE_TXBUF_OFFSET));
        cell_8 = ARCH_PTR_TO_VA(sio_$term_addr(line_off, -SIO_LINE_DATA_ADJUST));
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_DESC((sio_desc_t *)(void *)sio_$term_addr(desc_off, SIO_DESC_BASE_OFFSET),
                       sio_$term_addr(0, SIO_GENERIC_PARAM_OFFSET),
                       dtte,
                       &cell_8,
                       &cell_c,
                       (m68k_ptr_t *)(void *)sio_$term_addr(0, SIO_GENERIC_HANDLER_OFFSET),
                       (m68k_ptr_t *)context_ptr,
                       (char *)vtable_ptr);

        /* 0x00E32E36-0x00E32E5A */
        dtte = &TERM_$DATA.dtte[TERM_$MAX_DTTE];
        SIO_$INIT_DTTE(dtte, 0);

        /* 0x00E32E5C-0x00E32E88 */
        if (flags < 0) {
            TTY_$I_ENABLE_CRASH_FUNC((tty_desc_t *)(void *)sio_$term_addr(line_off, -SIO_LINE_DATA_ADJUST),
                                     0x1B, (char)true);
            *status_ret = status_$ok;
        } else {
            desc = (sio_desc_t *)(void *)sio_$term_addr(desc_off, SIO_DESC_BASE_OFFSET);
            ((sio_set_params_fn_t)ARCH_VA_TO_PTR(desc->set_params))(
                desc->context, &desc->params, SIO_SET_PARAMS_ALL_MASK, status_ret);
        }
    }

    /* 0x00E32E8A-0x00E32EAC */
    *desc_ret = (sio_desc_t *)(void *)sio_$term_addr(desc_off, SIO_DESC_BASE_OFFSET);
    TERM_$MAX_DTTE++;
}
