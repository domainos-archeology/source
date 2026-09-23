/*
 * SIO_$INIT_DESC - Fill in an SIO descriptor
 *
 * Copies the context and owner cells, the 0x16-byte parameter block, the
 * four handler pointers, four vtable entries (from vtable+0x14) and the
 * transmit buffer pointer into the descriptor, stores the descriptor's
 * address in the DTTE's tty_handler cell, and calls SIO_$I_INIT.
 * Called by SIO_$INIT (0x00E32D5C, 0x00E32E2E) and TERM_$INIT.
 *
 * Original address: 0x00E32AB2, 116 bytes (SAU2 map: OS_TERM segment)
 *
 *   00e32ab2    link.w A6,-0x4
 *   00e32ab6    movem.l {A4 A3 A2},-(SP)
 *   00e32aba    movea.l (0x8,A6),A2            ; arg 1 desc
 *   00e32abe    movea.l (0x10,A6),A0           ; arg 3 dtte
 *   00e32ac2    movea.l (0x20,A6),A1           ; arg 7 context_ptr
 *   00e32ac6    move.l (A1),(A2)               ; desc->context = *context_ptr
 *   00e32ac8    movea.l (0x14,A6),A3           ; arg 4 owner_ptr
 *   00e32acc    move.l (A3),(0x4,A2)           ; desc->owner = *owner_ptr
 *   00e32ad0    movea.l (0xc,A6),A4            ; arg 2 param_block
 *   00e32ad4    lea (0x4c,A2),A1
 *   00e32ad8    moveq #0x4,D0                  ; 5 longwords
 *   00e32ada    movea.l A0,A0
 *   00e32adc    move.l (A4)+,(A1)+
 *   00e32ade    dbf D0w,0x00e32adc
 *   00e32ae2    move.w (A4)+,(A1)+             ; + 1 word = 0x16 bytes
 *   00e32ae4    movea.l (0x1c,A6),A1           ; arg 6 handlers
 *   00e32ae8    lea (0x28,A2),A4
 *   00e32aec    move.l (A1)+,(A4)+             ; rcv_handler
 *   00e32aee    move.l (A1)+,(A4)+             ; drain_handler
 *   00e32af0    move.l (A1)+,(A4)+             ; dcd_handler
 *   00e32af2    move.l (A1)+,(A4)+             ; special_rcv
 *   00e32af4    movea.l (0x24,A6),A1           ; arg 8 vtable
 *   00e32af8    lea (0x3c,A2),A3
 *   00e32afc    lea (0x14,A1),A4
 *   00e32b00    move.l (A4)+,(A3)+             ; output_char  = vtable+0x14
 *   00e32b02    move.l (A4)+,(A3)+             ; set_params   = vtable+0x18
 *   00e32b04    move.l (A4)+,(A3)+             ; inq_params   = vtable+0x1C
 *   00e32b06    move.l (A4)+,(A3)+             ; set_break    = vtable+0x20
 *   00e32b08    movea.l (0x18,A6),A3           ; arg 5 txbuf_ptr
 *   00e32b0c    move.l (A3),(0x24,A2)          ; desc->txbuf = *txbuf_ptr
 *   00e32b10    move.l A2,(0x28,A0)            ; dtte->tty_handler = desc
 *   00e32b14    pea (A2)
 *   00e32b16    jsr 0x00e67e5e.l               ; SIO_$I_INIT(desc)
 *   00e32b1c    movem.l (-0x10,A6),{A2 A3 A4}
 *   00e32b22    unlk A6
 *   00e32b24    rts
 */

#include "sio/sio_internal.h"

void SIO_$INIT_DESC(sio_desc_t *desc, void *param_block, void *dtte,
                    m68k_ptr_t *owner_ptr, m68k_ptr_t *txbuf_ptr,
                    m68k_ptr_t *handlers, m68k_ptr_t *context_ptr,
                    char *vtable)
{
    const uint8_t *src;
    uint8_t *dst;
    int16_t i;
    const m68k_ptr_t *vt;

    /* 0x00E32AC6-0x00E32ACC */
    desc->context = *context_ptr;
    desc->owner = *owner_ptr;

    /* 0x00E32AD4-0x00E32AE2: five longwords then a word, as byte copies so
     * the packed record needs no aligned access */
    src = (const uint8_t *)param_block;
    dst = (uint8_t *)&desc->params;
    for (i = 4; i >= 0; i--) {
        memcpy(dst, src, 4);
        src += 4;
        dst += 4;
    }
    memcpy(dst, src, 2);

    /* 0x00E32AE4-0x00E32AF2 */
    desc->rcv_handler = handlers[0];
    desc->drain_handler = handlers[1];
    desc->dcd_handler = handlers[2];
    desc->special_rcv = handlers[3];

    /* 0x00E32AF4-0x00E32B06 */
    vt = (const m68k_ptr_t *)(void *)(vtable + 0x14);
    desc->output_char = vt[0];
    desc->set_params = vt[1];
    desc->inq_params = vt[2];
    desc->set_break = vt[3];

    /* 0x00E32B08-0x00E32B10 */
    desc->txbuf = *txbuf_ptr;
    ((dtte_t *)dtte)->tty_handler = ARCH_PTR_TO_VA(desc);

    /* 0x00E32B14-0x00E32B16 */
    SIO_$I_INIT(desc);
}
